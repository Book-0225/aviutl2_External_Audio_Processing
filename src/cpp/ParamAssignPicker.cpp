#include "ParamAssignPicker.h"

namespace {

static constexpr const wchar_t* APP_CLASS = L"ParamAssignPickerWindowClass";
constexpr int32_t kListBoxId = 1001;
constexpr int32_t kOkButtonId = 1002;
constexpr int32_t kCancelButtonId = 1003;
constexpr int32_t kWindowWidth = 420;
constexpr int32_t kWindowHeight = 320;

struct PickerState {
    const std::vector<ParamAssignCandidate>* candidates = nullptr;
    int32_t selectedIndex = -1;
    bool confirmed = false;
    HWND listBox = nullptr;
};

void ConfirmSelection(HWND hWnd, PickerState* state) {
    if (!state || !state->listBox) {
        DestroyWindow(hWnd);
        return;
    }
    int32_t sel = static_cast<int32_t>(SendMessage(state->listBox, LB_GETCURSEL, 0, 0));
    if (sel != LB_ERR) {
        state->selectedIndex = sel;
        state->confirmed = true;
    }
    DestroyWindow(hWnd);
}

LRESULT CALLBACK PickerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PickerState* state = reinterpret_cast<PickerState*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            state = reinterpret_cast<PickerState*>(cs->lpCreateParams);
            SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

            RECT rc;
            GetClientRect(hWnd, &rc);
            int32_t width = rc.right - rc.left;

            CreateWindowExW(0, L"STATIC", L"割り当てるパラメータを選択してください", WS_CHILD | WS_VISIBLE, 12, 10, width - 24, 20, hWnd, nullptr, cs->hInstance, nullptr);

            state->listBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS, 12, 36, width - 24, 200, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kListBoxId)), cs->hInstance, nullptr);

            if (state->candidates) {
                for (const auto& c : *state->candidates) {
                    SendMessage(state->listBox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(c.name.c_str()));
                }
                if (!state->candidates->empty()) SendMessage(state->listBox, LB_SETCURSEL, 0, 0);
            }

            CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, width - 174, 246, 80, 26, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOkButtonId)), cs->hInstance, nullptr);
            CreateWindowExW(0, L"BUTTON", L"キャンセル", WS_CHILD | WS_VISIBLE, width - 88, 246, 80, 26, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCancelButtonId)), cs->hInstance, nullptr);
            return 0;
        }

        case WM_COMMAND: {
            int32_t id = LOWORD(wParam);
            int32_t notifyCode = HIWORD(wParam);
            if (id == kOkButtonId) {
                ConfirmSelection(hWnd, state);
                return 0;
            }
            if (id == kListBoxId && notifyCode == LBN_DBLCLK) {
                ConfirmSelection(hWnd, state);
                return 0;
            }
            if (id == kCancelButtonId) {
                DestroyWindow(hWnd);
                return 0;
            }
            break;
        }
        case WM_KEYDOWN: {
            if (wParam == VK_RETURN) {
                ConfirmSelection(hWnd, state);
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                DestroyWindow(hWnd);
                return 0;
            }
            break;
        }

        case WM_CLOSE:
            DestroyWindow(hWnd);
            return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

} // namespace

int32_t ShowParamAssignPicker(HINSTANCE hInstance, HWND parentWindow, const std::wstring& dialogTitle, const std::vector<ParamAssignCandidate>& candidates) {
    if (candidates.empty()) return -1;
    if (candidates.size() == 1) return candidates[0].index;

    WNDCLASS existingClass;
    if (!GetClassInfoW(hInstance, APP_CLASS, &existingClass)) {
        WNDCLASS wc = {};
        wc.lpfnWndProc = PickerWndProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = APP_CLASS;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        if (!RegisterClass(&wc)) return -1;
    }

    PickerState state;
    state.candidates = &candidates;

    int32_t x = CW_USEDEFAULT;
    int32_t y = CW_USEDEFAULT;
    RECT parentRect{ 0, 0, 0, 0 };
    if (parentWindow && IsWindow(parentWindow) && GetWindowRect(parentWindow, &parentRect)) {
        x = parentRect.left + ((parentRect.right - parentRect.left) - kWindowWidth) / 2;
        y = parentRect.top + ((parentRect.bottom - parentRect.top) - kWindowHeight) / 2;
    }

    bool parentWasEnabled = false;
    bool hasValidParent = parentWindow && IsWindow(parentWindow);
    if (hasValidParent) {
        parentWasEnabled = IsWindowEnabled(parentWindow) != FALSE;
        EnableWindow(parentWindow, FALSE);
    }

    HWND hWnd = CreateWindowExW(WS_EX_DLGMODALFRAME, APP_CLASS, dialogTitle.c_str(), WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, kWindowWidth, kWindowHeight, parentWindow, nullptr, hInstance, &state);

    if (!hWnd) {
        if (hasValidParent) EnableWindow(parentWindow, TRUE);
        return -1;
    }

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);
    SetForegroundWindow(hWnd);

    MSG msg;
    while (IsWindow(hWnd) && GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (!IsWindow(hWnd)) break;
    }

    if (hasValidParent) {
        EnableWindow(parentWindow, parentWasEnabled ? TRUE : FALSE);
        SetForegroundWindow(parentWindow);
    }

    if (state.confirmed && state.selectedIndex >= 0 &&
        state.selectedIndex < static_cast<int32_t>(candidates.size())) {
        return candidates[state.selectedIndex].index;
    }
    return -1;
}
