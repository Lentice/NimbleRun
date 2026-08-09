#include "ui/panel_accessibility.h"

#include <algorithm>
#include <memory>
#include <new>

namespace nimblerun {
namespace {

constexpr LONG kSearchChild = 1;
constexpr LONG kFirstRowChild = 2;

std::wstring PageValue(const PanelAccessibilitySnapshot& snapshot) {
    return L"Query: " + snapshot.query + L"; Page " +
           std::to_wstring(snapshot.page) + L" of " +
           std::to_wstring(snapshot.page_count);
}

bool IsChildId(VARIANT id) noexcept {
    return id.vt == VT_I4;
}

bool SameRect(const RECT& left, const RECT& right) noexcept {
    return left.left == right.left && left.top == right.top &&
           left.right == right.right && left.bottom == right.bottom;
}

}  // namespace

struct PanelAccessibilityProvider::State {
    PanelAccessibilitySnapshot snapshot;
};

PanelAccessibilityProvider* PanelAccessibilityProvider::Create(HWND window) noexcept {
    try {
        auto state = std::make_shared<State>();
        return new PanelAccessibilityProvider(window, std::move(state), CHILDID_SELF,
                                              nullptr);
    } catch (...) {
        return nullptr;
    }
}

PanelAccessibilityProvider::PanelAccessibilityProvider(
    HWND window, std::shared_ptr<State> state, LONG child_id,
    PanelAccessibilityProvider* parent) noexcept
    : window_(window), state_(std::move(state)), child_id_(child_id), parent_(parent) {
    if (parent_ != nullptr) {
        parent_->AddRef();
    }
}

PanelAccessibilityProvider::~PanelAccessibilityProvider() {
    if (parent_ != nullptr) {
        parent_->Release();
    }
}

bool PanelAccessibilityProvider::Update(
    HWND window, const PanelAccessibilitySnapshot& snapshot) noexcept {
    try {
        const PanelAccessibilitySnapshot old = state_->snapshot;
        PanelAccessibilitySnapshot replacement = snapshot;
        state_->snapshot = std::move(replacement);
        window_ = window;

        const bool structure_changed =
            old.query != snapshot.query || old.page != snapshot.page ||
            old.page_count != snapshot.page_count || old.rows.size() != snapshot.rows.size() ||
            !SameRect(old.search_bounds, snapshot.search_bounds) ||
            !SameRect(old.footer_bounds, snapshot.footer_bounds);
        if (window_ != nullptr && structure_changed) {
            NotifyWinEvent(EVENT_OBJECT_REORDER, window_, OBJID_CLIENT, CHILDID_SELF);
        }
        if (window_ != nullptr && old.selected_row != snapshot.selected_row) {
            const LONG child = snapshot.selected_row >= 0
                ? kFirstRowChild + snapshot.selected_row : CHILDID_SELF;
            NotifyWinEvent(EVENT_OBJECT_SELECTION, window_, OBJID_CLIENT, child);
            NotifyWinEvent(EVENT_OBJECT_FOCUS, window_, OBJID_CLIENT, child);
        }
        if (window_ != nullptr) {
            const std::size_t count = std::min(old.rows.size(), snapshot.rows.size());
            for (std::size_t i = 0; i < count; ++i) {
                if (old.rows[i].name != snapshot.rows[i].name) {
                    NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, window_, OBJID_CLIENT,
                                   kFirstRowChild + static_cast<LONG>(i));
                }
                if (old.rows[i].disabled != snapshot.rows[i].disabled) {
                    NotifyWinEvent(EVENT_OBJECT_STATECHANGE, window_, OBJID_CLIENT,
                                   kFirstRowChild + static_cast<LONG>(i));
                }
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

LRESULT PanelAccessibilityProvider::OnGetObject(WPARAM w_param, LPARAM l_param) noexcept {
    if (l_param != OBJID_CLIENT) {
        return 0;
    }
    return LresultFromObject(IID_IAccessible, w_param,
                             static_cast<IUnknown*>(static_cast<IAccessible*>(this)));
}

HRESULT STDMETHODCALLTYPE PanelAccessibilityProvider::QueryInterface(
    REFIID riid, void** object) {
    if (object == nullptr) {
        return E_POINTER;
    }
    *object = nullptr;
    if (riid == IID_IUnknown || riid == IID_IDispatch || riid == IID_IAccessible) {
        *object = static_cast<IAccessible*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE PanelAccessibilityProvider::AddRef() {
    return ++refs_;
}

ULONG STDMETHODCALLTYPE PanelAccessibilityProvider::Release() {
    const ULONG refs = --refs_;
    if (refs == 0) {
        delete this;
    }
    return refs;
}

HRESULT STDMETHODCALLTYPE PanelAccessibilityProvider::GetTypeInfoCount(UINT* count) {
    if (count == nullptr) {
        return E_POINTER;
    }
    *count = 0;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE PanelAccessibilityProvider::GetTypeInfo(UINT, LCID, ITypeInfo**) {
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE PanelAccessibilityProvider::GetIDsOfNames(
    REFIID, LPOLESTR*, UINT, LCID, DISPID*) {
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE PanelAccessibilityProvider::Invoke(
    DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) {
    return E_NOTIMPL;
}

const PanelAccessibilityElement* PanelAccessibilityProvider::Element(
    LONG child_id) const noexcept {
    const auto& snapshot = state_->snapshot;
    if (child_id == kSearchChild) {
        static const PanelAccessibilityElement search{
            PanelAccessibilityElement::Role::SearchField, L"Search apps", {}, false, false};
        return &search;
    }
    if (child_id >= kFirstRowChild &&
        child_id < kFirstRowChild + static_cast<LONG>(snapshot.rows.size())) {
        return &snapshot.rows[static_cast<std::size_t>(child_id - kFirstRowChild)];
    }
    if (child_id == kFirstRowChild + static_cast<LONG>(snapshot.rows.size())) {
        static const PanelAccessibilityElement footer{
            PanelAccessibilityElement::Role::Footer, L"Footer", {}, false, false};
        return &footer;
    }
    return nullptr;
}

const PanelAccessibilityElement* PanelAccessibilityProvider::ElementFor(VARIANT id) const noexcept {
    if (!IsChildId(id)) {
        return nullptr;
    }
    return id.lVal == CHILDID_SELF
        ? (IsRoot() ? nullptr : Element(child_id_)) : Element(id.lVal);
}

LONG PanelAccessibilityProvider::ChildCount() const noexcept {
    return IsRoot() ? 2 + static_cast<LONG>(state_->snapshot.rows.size()) : 0;
}

HRESULT PanelAccessibilityProvider::PutString(BSTR* output,
                                               const std::wstring& value) const noexcept {
    if (output == nullptr) {
        return E_POINTER;
    }
    *output = SysAllocStringLen(value.data(), static_cast<UINT>(value.size()));
    return *output == nullptr && !value.empty() ? E_OUTOFMEMORY : S_OK;
}

HRESULT PanelAccessibilityProvider::PutChild(VARIANT* output, LONG child_id) const noexcept {
    if (output == nullptr) {
        return E_POINTER;
    }
    VariantInit(output);
    output->vt = VT_I4;
    output->lVal = child_id;
    return S_OK;
}

LONG PanelAccessibilityProvider::FirstChild() const noexcept {
    return ChildCount() > 0 ? kSearchChild : CHILDID_SELF;
}

LONG PanelAccessibilityProvider::LastChild() const noexcept {
    return ChildCount() > 0 ? kFirstRowChild + static_cast<LONG>(state_->snapshot.rows.size())
                            : CHILDID_SELF;
}

LONG PanelAccessibilityProvider::NextChild(LONG child_id, LONG direction) const noexcept {
    if (!IsRoot()) {
        return CHILDID_SELF;
    }
    const LONG first = FirstChild();
    const LONG last = LastChild();
    if (direction == NAVDIR_NEXT) {
        return child_id < last ? child_id + 1 : CHILDID_SELF;
    }
    return child_id > first ? child_id - 1 : CHILDID_SELF;
}

HRESULT STDMETHODCALLTYPE PanelAccessibilityProvider::get_accParent(IDispatch** parent) {
    if (parent == nullptr) {
        return E_POINTER;
    }
    *parent = nullptr;
    if (parent_ == nullptr) {
        return S_FALSE;
    }
    return parent_->QueryInterface(IID_IDispatch, reinterpret_cast<void**>(parent));
}

HRESULT STDMETHODCALLTYPE PanelAccessibilityProvider::get_accChildCount(LONG* count) {
    if (count == nullptr) {
        return E_POINTER;
    }
    *count = ChildCount();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE PanelAccessibilityProvider::get_accChild(VARIANT child_id,
                                                                     IDispatch** child) {
    if (child == nullptr) {
        return E_POINTER;
    }
    *child = nullptr;
    if (!IsRoot() || !IsChildId(child_id) || Element(child_id.lVal) == nullptr) {
        return E_INVALIDARG;
    }
    try {
        auto* result = new PanelAccessibilityProvider(window_, state_, child_id.lVal, this);
        *child = static_cast<IDispatch*>(static_cast<IAccessible*>(result));
        return S_OK;
    } catch (...) {
        return E_OUTOFMEMORY;
    }
}

HRESULT STDMETHODCALLTYPE PanelAccessibilityProvider::get_accName(VARIANT id, BSTR* name) {
    if (name == nullptr) {
        return E_POINTER;
    }
    *name = nullptr;
    if (IsRoot() && IsChildId(id) && id.lVal == CHILDID_SELF) {
        return PutString(name, L"NimbleRun");
    }
    if (!IsChildId(id)) {
        return E_INVALIDARG;
    }
    if (id.lVal == kSearchChild) {
        return PutString(name, L"Search apps");
    }
    if (const auto* element = ElementFor(id)) {
        return PutString(name, element->role == PanelAccessibilityElement::Role::Footer
                                ? L"Footer" : element->name);
    }
    return E_INVALIDARG;
}

HRESULT STDMETHODCALLTYPE PanelAccessibilityProvider::get_accValue(VARIANT id, BSTR* value) {
    if (value == nullptr) {
        return E_POINTER;
    }
    *value = nullptr;
    if (IsRoot() && IsChildId(id) && id.lVal == CHILDID_SELF) {
        return PutString(value, PageValue(state_->snapshot));
    }
    if (!IsChildId(id)) {
        return E_INVALIDARG;
    }
    if (id.lVal == kSearchChild) {
        return PutString(value, state_->snapshot.query);
    }
    if (const auto* element = ElementFor(id)) {
        return PutString(value, element->role == PanelAccessibilityElement::Role::Footer
                                ? state_->snapshot.footer : L"");
    }
    return E_INVALIDARG;
}

HRESULT STDMETHODCALLTYPE PanelAccessibilityProvider::get_accDescription(
    VARIANT id, BSTR* description) {
    if (description == nullptr) {
        return E_POINTER;
    }
    *description = nullptr;
    if (!IsChildId(id)) {
        return E_INVALIDARG;
    }
    if (id.lVal == kSearchChild) {
        return PutString(description, L"Type to filter apps");
    }
    if (const auto* element = ElementFor(id)) {
        if (element->disabled) {
            return PutString(description, L"Missing pinned app");
        }
        return PutString(description, element->role == PanelAccessibilityElement::Role::Footer
                                         ? L"Current panel state" : L"");
    }
    return E_INVALIDARG;
}

HRESULT STDMETHODCALLTYPE PanelAccessibilityProvider::get_accRole(VARIANT id, VARIANT* role) {
    if (role == nullptr) {
        return E_POINTER;
    }
    VariantInit(role);
    if (IsRoot() && IsChildId(id) && id.lVal == CHILDID_SELF) {
        role->vt = VT_I4;
        role->lVal = ROLE_SYSTEM_CLIENT;
        return S_OK;
    }
    const auto* element = ElementFor(id);
    if (element == nullptr) {
        return E_INVALIDARG;
    }
    role->vt = VT_I4;
    role->lVal = element->role == PanelAccessibilityElement::Role::SearchField
        ? ROLE_SYSTEM_TEXT
        : element->role == PanelAccessibilityElement::Role::AppRow
            ? ROLE_SYSTEM_LISTITEM : ROLE_SYSTEM_STATICTEXT;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE PanelAccessibilityProvider::get_accState(VARIANT id, VARIANT* state) {
    if (state == nullptr) {
        return E_POINTER;
    }
    VariantInit(state);
    LONG flags = 0;
    if (IsRoot() && IsChildId(id) && id.lVal == CHILDID_SELF) {
        state->vt = VT_I4;
        state->lVal = flags;
        return S_OK;
    }
    const auto* element = ElementFor(id);
    if (element == nullptr) {
        return E_INVALIDARG;
    }
    if (element->role == PanelAccessibilityElement::Role::SearchField) {
        flags |= STATE_SYSTEM_FOCUSABLE;
        if (state_->snapshot.search_focused) {
            flags |= STATE_SYSTEM_FOCUSED;
        }
    } else if (element->role == PanelAccessibilityElement::Role::AppRow) {
        flags |= STATE_SYSTEM_FOCUSABLE | STATE_SYSTEM_SELECTABLE;
        if (element->selected) {
            flags |= STATE_SYSTEM_SELECTED | STATE_SYSTEM_FOCUSED;
        }
        if (element->disabled) {
            flags |= STATE_SYSTEM_UNAVAILABLE;
        }
    } else {
        flags |= STATE_SYSTEM_READONLY;
    }
    state->vt = VT_I4;
    state->lVal = flags;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE PanelAccessibilityProvider::get_accHelp(VARIANT, BSTR* help) {
    if (help == nullptr) {
        return E_POINTER;
    }
    *help = nullptr;
    return S_FALSE;
}

HRESULT STDMETHODCALLTYPE PanelAccessibilityProvider::get_accHelpTopic(BSTR* help_file,
                                                                        VARIANT,
                                                                        LONG* topic) {
    if (help_file == nullptr || topic == nullptr) {
        return E_POINTER;
    }
    *help_file = nullptr;
    *topic = 0;
    return S_FALSE;
}

HRESULT STDMETHODCALLTYPE PanelAccessibilityProvider::get_accKeyboardShortcut(
    VARIANT, BSTR* shortcut) {
    if (shortcut == nullptr) {
        return E_POINTER;
    }
    *shortcut = nullptr;
    return S_FALSE;
}

HRESULT STDMETHODCALLTYPE PanelAccessibilityProvider::get_accFocus(VARIANT* id) {
    if (id == nullptr) {
        return E_POINTER;
    }
    VariantInit(id);
    if (!IsRoot()) {
        return S_FALSE;
    }
    if (state_->snapshot.search_focused) {
        return PutChild(id, kSearchChild);
    }
    if (state_->snapshot.selected_row >= 0) {
        return PutChild(id, kFirstRowChild + state_->snapshot.selected_row);
    }
    return S_FALSE;
}

HRESULT STDMETHODCALLTYPE PanelAccessibilityProvider::get_accSelection(VARIANT* id) {
    if (id == nullptr) {
        return E_POINTER;
    }
    VariantInit(id);
    if (!IsRoot() || state_->snapshot.selected_row < 0) {
        return S_FALSE;
    }
    return PutChild(id, kFirstRowChild + state_->snapshot.selected_row);
}

HRESULT STDMETHODCALLTYPE PanelAccessibilityProvider::get_accDefaultAction(VARIANT id,
                                                                            BSTR* action) {
    if (action == nullptr) {
        return E_POINTER;
    }
    *action = nullptr;
    if (!IsChildId(id)) {
        return E_INVALIDARG;
    }
    const auto* element = ElementFor(id);
    if (element == nullptr || element->role != PanelAccessibilityElement::Role::AppRow) {
        return S_FALSE;
    }
    return PutString(action, element->disabled ? L"" : L"Open");
}

HRESULT STDMETHODCALLTYPE PanelAccessibilityProvider::accSelect(LONG, VARIANT) {
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE PanelAccessibilityProvider::accLocation(
    LONG* left, LONG* top, LONG* width, LONG* height, VARIANT id) {
    if (left == nullptr || top == nullptr || width == nullptr || height == nullptr) {
        return E_POINTER;
    }
    RECT bounds{};
    if (IsRoot() && IsChildId(id) && id.lVal == CHILDID_SELF) {
        if (window_ == nullptr || !GetWindowRect(window_, &bounds)) {
            return E_FAIL;
        }
    } else {
        const auto* element = ElementFor(id);
        if (element == nullptr) {
            return E_INVALIDARG;
        }
        if (id.lVal == kSearchChild) {
            bounds = state_->snapshot.search_bounds;
        } else if (id.lVal == kFirstRowChild +
                   static_cast<LONG>(state_->snapshot.rows.size())) {
            bounds = state_->snapshot.footer_bounds;
        } else {
            bounds = element->bounds;
        }
    }
    *left = bounds.left;
    *top = bounds.top;
    *width = bounds.right - bounds.left;
    *height = bounds.bottom - bounds.top;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE PanelAccessibilityProvider::accNavigate(LONG direction,
                                                                   VARIANT start,
                                                                   VARIANT* end) {
    if (end == nullptr) {
        return E_POINTER;
    }
    VariantInit(end);
    if (!IsRoot() || !IsChildId(start)) {
        return E_INVALIDARG;
    }
    LONG child = CHILDID_SELF;
    if (direction == NAVDIR_FIRSTCHILD) {
        child = FirstChild();
    } else if (direction == NAVDIR_LASTCHILD) {
        child = LastChild();
    } else if (direction == NAVDIR_NEXT || direction == NAVDIR_PREVIOUS) {
        child = NextChild(start.lVal, direction);
    } else {
        return E_NOTIMPL;
    }
    if (child == CHILDID_SELF) {
        return S_FALSE;
    }
    return PutChild(end, child);
}

HRESULT STDMETHODCALLTYPE PanelAccessibilityProvider::accHitTest(LONG x, LONG y,
                                                                  VARIANT* id) {
    if (id == nullptr) {
        return E_POINTER;
    }
    VariantInit(id);
    if (!IsRoot()) {
        return E_NOTIMPL;
    }
    const auto& snapshot = state_->snapshot;
    for (std::size_t i = 0; i < snapshot.rows.size(); ++i) {
        const RECT& bounds = snapshot.rows[i].bounds;
        if (x >= bounds.left && x < bounds.right && y >= bounds.top && y < bounds.bottom) {
            return PutChild(id, kFirstRowChild + static_cast<LONG>(i));
        }
    }
    const RECT* fixed_bounds[] = {&snapshot.search_bounds, &snapshot.footer_bounds};
    const LONG fixed_children[] = {kSearchChild,
                                   kFirstRowChild + static_cast<LONG>(snapshot.rows.size())};
    for (int i = 0; i < 2; ++i) {
        if (PtInRect(fixed_bounds[i], POINT{x, y})) {
            return PutChild(id, fixed_children[i]);
        }
    }
    RECT window_bounds{};
    if (window_ != nullptr) {
        GetWindowRect(window_, &window_bounds);
    }
    if (PtInRect(&window_bounds, POINT{x, y})) {
        return PutChild(id, CHILDID_SELF);
    }
    return S_FALSE;
}

HRESULT STDMETHODCALLTYPE PanelAccessibilityProvider::accDoDefaultAction(VARIANT) {
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE PanelAccessibilityProvider::put_accName(VARIANT, BSTR) {
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE PanelAccessibilityProvider::put_accValue(VARIANT, BSTR) {
    return E_NOTIMPL;
}

}  // namespace nimblerun
