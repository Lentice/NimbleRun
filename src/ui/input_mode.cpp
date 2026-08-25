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

// TSF path: set the thread-manager keyboard-input conversion compartment to
// alphanumeric. Requires an active TSF client on the caller thread (the
// panel's UI thread is already STA COM); any failure falls through so the
// caller can try IMM32. TSF operates on thread-level focus, so the edit HWND
// is not needed on this path.
bool TryTsf() {
    ITfThreadMgr* raw_mgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_ITfThreadMgr,
                                  reinterpret_cast<void**>(&raw_mgr));
    if (FAILED(hr) || raw_mgr == nullptr) {
        return false;
    }
    ComPtr<ITfThreadMgr> thread_mgr(raw_mgr);

    TfClientId client_id = 0;
    if (FAILED(thread_mgr->Activate(&client_id))) {
        return false;
    }
    // Activate is ref-counted per thread; every successful call must be paired
    // with Deactivate, including all failure paths below.
    const auto deactivateAndReturn = [&thread_mgr](bool result) {
        thread_mgr->Deactivate();
        return result;
    };

    BOOL thread_focus = FALSE;
    // A native EDIT can have TSF input focus without exposing a document
    // manager through GetFocus; IsThreadFocus is the correct availability gate.
    if (FAILED(thread_mgr->IsThreadFocus(&thread_focus)) || thread_focus == FALSE) {
        return deactivateAndReturn(false);
    }

    ITfCompartmentMgr* raw_comp_mgr = nullptr;
    // GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION is a thread-manager
    // compartment. A context-local compartment can accept SetValue while
    // leaving the focused IME's actual conversion mode unchanged.
    if (FAILED(thread_mgr->QueryInterface(IID_ITfCompartmentMgr,
                                       reinterpret_cast<void**>(&raw_comp_mgr))) ||
        raw_comp_mgr == nullptr) {
        return deactivateAndReturn(false);
    }
    ComPtr<ITfCompartmentMgr> comp_mgr(raw_comp_mgr);

    ITfCompartment* raw_compartment = nullptr;
    if (FAILED(comp_mgr->GetCompartment(kCompartmentKeyboardInputConversion,
                                        &raw_compartment)) ||
        raw_compartment == nullptr) {
        return deactivateAndReturn(false);
    }
    ComPtr<ITfCompartment> compartment(raw_compartment);

    // VT_I4 owns nothing, so a zero-initialized VARIANT needs no
    // VariantInit/VariantClear (avoids an oleaut32 dependency).
    VARIANT value{};
    value.vt = VT_I4;
    value.lVal = input_mode_detail::kTsfConversionModeAlphanumeric;
    return deactivateAndReturn(SUCCEEDED(compartment->SetValue(client_id, &value)));
}

// IMM32 fallback for standard IMM-based IMEs. Keeps the IME open and clears
// the native-conversion bit so Latin letters enter directly; the rest of the
// IME state (sentence mode etc.) is left untouched.
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
            conversion &= static_cast<DWORD>(~IME_CMODE_NATIVE);
            ok = ImmSetConversionStatus(context, conversion, sentence) != FALSE;
        }
    }
    ImmReleaseContext(edit, context);
    return ok;
}

} // namespace

bool ShouldSetEnglishInputMode(bool enabled, bool was_visible) {
    return enabled && !was_visible;
}

bool SetEnglishInputMode(HWND edit) {
    if (edit == nullptr || IsWindow(edit) == FALSE) {
        return false;
    }
    if (TryTsf()) {
        return true;
    }
    return TryImm(edit);
}

void WarmUpInputMode() {
    // NR-198: same Activate/Deactivate pair as TryTsf, minus the
    // IsThreadFocus/compartment work -- this call's only job is to make TSF
    // install its focus-tracking hooks before a real SetFocus happens.
    ITfThreadMgr* raw_mgr = nullptr;
    const HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr,
                                        CLSCTX_INPROC_SERVER, IID_ITfThreadMgr,
                                        reinterpret_cast<void**>(&raw_mgr));
    if (FAILED(hr) || raw_mgr == nullptr) {
        return;
    }
    ComPtr<ITfThreadMgr> thread_mgr(raw_mgr);
    TfClientId client_id = 0;
    if (SUCCEEDED(thread_mgr->Activate(&client_id))) {
        thread_mgr->Deactivate();
    }
}

} // namespace nimblerun
