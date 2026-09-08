#include "ui/input_mode.h"

#include "win/com.h"

#include <imm.h>
#include <msctf.h>

namespace nimblerun {
namespace {

// LLVM-MinGW's msctf.h stops before the keyboard-input-mode compartments and
// the TF_CONVERSIONMODE values (those live in msimtf.h/ctffunc.h in the MS
// SDK). The documented GUID is declared here; no third-party headers.
// {CCF05DD8-4A87-11D7-A6E2-00065B84435C}
constexpr GUID kCompartmentKeyboardInputConversion = {
    0xCCF05DD8, 0x4A87, 0x11D7, {0xA6, 0xE2, 0x00, 0x06, 0x5B, 0x84, 0x43, 0x5C}};

// NR-199: ITfThreadMgr::Activate is a per-thread reference count, not a
// one-shot initialization. Every path here used to pair it with an immediate
// Deactivate, which is the bug: when this process holds the only activation on
// the UI thread, dropping it back to zero tears down the thread's TSF client
// state -- including the focus-tracking hooks WarmUpInputMode exists to install
// and the compartment value TryTsf just wrote. The switch then worked only when
// something else on the thread happened to be holding an activation, which is
// exactly the intermittent failure users saw.
//
// One activation is acquired on first use and held for the life of the process.
// The manager is deliberately never Released or Deactivated: it is UI-thread
// state that must outlive any static destructor ordering against CoUninitialize.
ITfThreadMgr* g_thread_mgr = nullptr;
TfClientId g_client_id = 0;

// Returns the UI thread's activated TSF manager, or nullptr when TSF is
// unusable (no COM apartment, no TSF). A failure is not cached: retrying costs
// one failed CoCreateInstance on a machine where this can never work anyway.
ITfThreadMgr* ActivatedThreadMgr() {
    if (g_thread_mgr != nullptr) {
        return g_thread_mgr;
    }
    ITfThreadMgr* raw_mgr = nullptr;
    const HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr,
                                        CLSCTX_INPROC_SERVER, IID_ITfThreadMgr,
                                        reinterpret_cast<void**>(&raw_mgr));
    if (FAILED(hr) || raw_mgr == nullptr) {
        return nullptr;
    }
    TfClientId client_id = 0;
    if (FAILED(raw_mgr->Activate(&client_id))) {
        raw_mgr->Release();
        return nullptr;
    }
    g_thread_mgr = raw_mgr;
    g_client_id = client_id;
    return g_thread_mgr;
}

// TSF path: set the thread-manager keyboard-input conversion compartment to
// alphanumeric. Any failure returns false; the caller runs IMM32 either way.
// TSF operates on thread-level focus, so the edit HWND is not needed here.
bool TryTsf() {
    ITfThreadMgr* thread_mgr = ActivatedThreadMgr();
    if (thread_mgr == nullptr) {
        return false;
    }

    BOOL thread_focus = FALSE;
    // A native EDIT can have TSF input focus without exposing a document
    // manager through GetFocus; IsThreadFocus is the correct availability gate.
    if (FAILED(thread_mgr->IsThreadFocus(&thread_focus)) || thread_focus == FALSE) {
        return false;
    }

    ITfCompartmentMgr* raw_comp_mgr = nullptr;
    // GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION is a thread-manager
    // compartment. A context-local compartment can accept SetValue while
    // leaving the focused IME's actual conversion mode unchanged.
    if (FAILED(thread_mgr->QueryInterface(IID_ITfCompartmentMgr,
                                       reinterpret_cast<void**>(&raw_comp_mgr))) ||
        raw_comp_mgr == nullptr) {
        return false;
    }
    ComPtr<ITfCompartmentMgr> comp_mgr(raw_comp_mgr);

    ITfCompartment* raw_compartment = nullptr;
    if (FAILED(comp_mgr->GetCompartment(kCompartmentKeyboardInputConversion,
                                        &raw_compartment)) ||
        raw_compartment == nullptr) {
        return false;
    }
    ComPtr<ITfCompartment> compartment(raw_compartment);

    // VT_I4 owns nothing, so a zero-initialized VARIANT needs no
    // VariantInit/VariantClear (avoids an oleaut32 dependency).
    VARIANT value{};
    value.vt = VT_I4;
    value.lVal = input_mode_detail::kTsfConversionModeAlphanumeric;
    return SUCCEEDED(compartment->SetValue(g_client_id, &value));
}

// IMM32 path for IMM-based IMEs and for the IMM32 shim in front of a TSF-only
// TIP. Keeps the IME open and clears the native-conversion bit so Latin letters
// enter directly. NR-199: FULLSHAPE goes with it -- leaving it set produced
// full-width Latin, which reads to the user as "it did not switch". The rest of
// the IME state (sentence mode etc.) is left untouched.
bool TryImm(HWND edit) {
    HIMC context = ImmGetContext(edit);
    if (context == nullptr) {
        return false;
    }
    bool ok = false;
    if (ImmSetOpenStatus(context, TRUE) != FALSE) {
        DWORD conversion = 0;
        DWORD sentence = 0;
        if (ImmGetConversionStatus(context, &conversion, &sentence) != FALSE) {
            conversion &= static_cast<DWORD>(~(IME_CMODE_NATIVE | IME_CMODE_FULLSHAPE));
            ok = ImmSetConversionStatus(context, conversion, sentence) != FALSE;
        }
    }
    ImmReleaseContext(edit, context);
    return ok;
}

// Best-effort switch of the search EDIT's IME input mode to alphanumeric
// (English). NR-199: runs BOTH the TSF thread-manager keyboard-input
// compartment and the IMM32 path -- a successful TSF SetValue is not proof the
// focused TIP changed mode, so it must not suppress IMM32. Returns true if
// either path reported success, false for a null/invalid HWND or when neither
// is usable; never throws, never blocks, never touches settings, and never
// changes the keyboard layout.
bool SetEnglishInputMode(HWND edit) {
    if (edit == nullptr || IsWindow(edit) == FALSE) {
        return false;
    }
    // NR-199: both paths run, and TSF no longer short-circuits IMM32.
    // ITfCompartment::SetValue returning S_OK only means the compartment now
    // holds the value -- it is not proof that the focused TIP observed it and
    // changed its conversion mode. Treating that S_OK as success meant the
    // IMM32 path, which is what actually works for several CJK IMEs, was never
    // reached. Running IMM32 after a successful TSF write is a no-op.
    const bool tsf_ok = TryTsf();
    const bool imm_ok = TryImm(edit);
    return tsf_ok || imm_ok;
}

} // namespace

EnglishInputModeTransition EnglishInputModeTransition::Prepare(
    bool enabled, bool was_visible) {
    // NR-190's gate, inlined: the switch happens only for a genuine
    // hidden->visible show with the setting enabled, so a re-show while the
    // panel is already visible never repeats it.
    const bool will_apply = enabled && !was_visible;
    if (will_apply) {
        // NR-198/NR-199: acquire the process-lifetime activation now, before
        // any real SetFocus, so TSF's focus-tracking hooks are installed in
        // time for this show. Idempotent and cheap after the first call.
        ActivatedThreadMgr();
    }
    return EnglishInputModeTransition(will_apply);
}

bool EnglishInputModeTransition::Apply(HWND edit) {
    if (!will_apply_) {
        return false;
    }
    // Consume before entering native code so even a failure cannot make a
    // repeated caller run TSF/IMM32 a second time.
    will_apply_ = false;
    return SetEnglishInputMode(edit);
}

} // namespace nimblerun
