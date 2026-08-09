#pragma once

#include <oleacc.h>

#include <memory>
#include <string>
#include <vector>

namespace nimblerun {

struct PanelAccessibilityElement {
    enum class Role { SearchField, AppRow, Footer };

    Role role = Role::AppRow;
    std::wstring name;
    RECT bounds{};
    bool selected = false;
    bool disabled = false;
};

struct PanelAccessibilitySnapshot {
    std::wstring query;
    std::wstring footer;
    RECT search_bounds{};
    RECT footer_bounds{};
    int page = 1;
    int page_count = 1;
    bool search_focused = false;
    int selected_row = -1;
    std::vector<PanelAccessibilityElement> rows;
};

class PanelAccessibilityProvider final : public IAccessible {
public:
    static PanelAccessibilityProvider* Create(HWND window) noexcept;

    bool Update(HWND window, const PanelAccessibilitySnapshot& snapshot) noexcept;
    LRESULT OnGetObject(WPARAM w_param, LPARAM l_param) noexcept;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) override;
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT index, LCID lcid, ITypeInfo** info) override;
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID riid, LPOLESTR* names,
                                            UINT count, LCID lcid,
                                            DISPID* disp_ids) override;
    HRESULT STDMETHODCALLTYPE Invoke(DISPID disp_id, REFIID riid, LCID lcid,
                                     WORD flags, DISPPARAMS* params,
                                     VARIANT* result, EXCEPINFO* exception,
                                     UINT* argument) override;

    HRESULT STDMETHODCALLTYPE get_accParent(IDispatch** parent) override;
    HRESULT STDMETHODCALLTYPE get_accChildCount(LONG* count) override;
    HRESULT STDMETHODCALLTYPE get_accChild(VARIANT child_id, IDispatch** child) override;
    HRESULT STDMETHODCALLTYPE get_accName(VARIANT id, BSTR* name) override;
    HRESULT STDMETHODCALLTYPE get_accValue(VARIANT id, BSTR* value) override;
    HRESULT STDMETHODCALLTYPE get_accDescription(VARIANT id, BSTR* description) override;
    HRESULT STDMETHODCALLTYPE get_accRole(VARIANT id, VARIANT* role) override;
    HRESULT STDMETHODCALLTYPE get_accState(VARIANT id, VARIANT* state) override;
    HRESULT STDMETHODCALLTYPE get_accHelp(VARIANT id, BSTR* help) override;
    HRESULT STDMETHODCALLTYPE get_accHelpTopic(BSTR* help_file, VARIANT id,
                                                LONG* topic) override;
    HRESULT STDMETHODCALLTYPE get_accKeyboardShortcut(VARIANT id,
                                                      BSTR* shortcut) override;
    HRESULT STDMETHODCALLTYPE get_accFocus(VARIANT* id) override;
    HRESULT STDMETHODCALLTYPE get_accSelection(VARIANT* id) override;
    HRESULT STDMETHODCALLTYPE get_accDefaultAction(VARIANT id,
                                                   BSTR* action) override;
    HRESULT STDMETHODCALLTYPE accSelect(LONG flags, VARIANT id) override;
    HRESULT STDMETHODCALLTYPE accLocation(LONG* left, LONG* top, LONG* width,
                                           LONG* height, VARIANT id) override;
    HRESULT STDMETHODCALLTYPE accNavigate(LONG direction, VARIANT start,
                                          VARIANT* end) override;
    HRESULT STDMETHODCALLTYPE accHitTest(LONG x, LONG y, VARIANT* id) override;
    HRESULT STDMETHODCALLTYPE accDoDefaultAction(VARIANT id) override;
    HRESULT STDMETHODCALLTYPE put_accName(VARIANT id, BSTR name) override;
    HRESULT STDMETHODCALLTYPE put_accValue(VARIANT id, BSTR value) override;

private:
    struct State;

    PanelAccessibilityProvider(HWND window, std::shared_ptr<State> state,
                               LONG child_id, PanelAccessibilityProvider* parent) noexcept;
    ~PanelAccessibilityProvider();

    const PanelAccessibilityElement* Element(LONG child_id) const noexcept;
    const PanelAccessibilityElement* ElementFor(VARIANT id) const noexcept;
    bool IsRoot() const noexcept { return child_id_ == CHILDID_SELF; }
    LONG ChildCount() const noexcept;
    HRESULT PutString(BSTR* output, const std::wstring& value) const noexcept;
    HRESULT PutChild(VARIANT* output, LONG child_id) const noexcept;
    LONG FirstChild() const noexcept;
    LONG LastChild() const noexcept;
    LONG NextChild(LONG child_id, LONG direction) const noexcept;

    ULONG refs_ = 1;
    HWND window_ = nullptr;
    std::shared_ptr<State> state_;
    LONG child_id_ = CHILDID_SELF;
    PanelAccessibilityProvider* parent_ = nullptr;
};

}  // namespace nimblerun
