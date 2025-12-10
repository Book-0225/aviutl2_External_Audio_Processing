#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include "IAudioPluginHost.h"

class ToolParamListWindow {
public:
    static ToolParamListWindow& GetInstance() {
        static ToolParamListWindow instance;
        return instance;
    }

    void Show(std::shared_ptr<IAudioPluginHost> host, const std::string& titleSuffix) {
        if (!host) return;
        m_host = host;

        if (m_hWnd && IsWindow(m_hWnd)) {
            ShowWindow(m_hWnd, SW_SHOW);
            SetForegroundWindow(m_hWnd);
            UpdateList();
            return;
        }

        std::wstring className = L"ToolParamListWindow";
        WNDCLASSEX wc = { 0 };
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = className.c_str();
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassEx(&wc);

        std::wstring windowTitle = L"Parameter List - " + std::wstring(titleSuffix.begin(), titleSuffix.end());
        m_hWnd = CreateWindowEx(0, className.c_str(), windowTitle.c_str(),
            WS_OVERLAPPED | WS_CAPTION, CW_USEDEFAULT, CW_USEDEFAULT, 500, 600,
            NULL, NULL, GetModuleHandle(NULL), this);

        if (m_hWnd) {
            ShowWindow(m_hWnd, SW_SHOW);
            UpdateList();
        }
    }

    void Close() {
        if (m_hWnd && IsWindow(m_hWnd)) DestroyWindow(m_hWnd);
    }

    void UpdateList() {
        if (!m_hWnd || !IsWindow(m_hWnd) || !m_listView) return;

        ListView_DeleteAllItems(m_listView);

        if (!m_host) return;
        int32_t count = m_host->GetParameterCount();

        for (int32_t i = 0; i < count; ++i) {
            IAudioPluginHost::ParameterInfo info = {};
            if (m_host->GetParameterInfo(i, info)) {
                std::wstring indexStr = std::to_wstring(i);
                LVITEM lvi = { 0 };
                lvi.mask = LVIF_TEXT;
                lvi.iItem = i;
                lvi.pszText = (LPWSTR)indexStr.c_str();
                ListView_InsertItem(m_listView, &lvi);

                std::string nameUtf8 = info.name;
                std::wstring nameWide(nameUtf8.begin(), nameUtf8.end());
                ListView_SetItemText(m_listView, i, 1, (LPWSTR)nameWide.c_str());

                std::wstring stepStr = std::to_wstring(info.step);
                ListView_SetItemText(m_listView, i, 2, (LPWSTR)stepStr.c_str());

                std::string unitUtf8 = info.unit;
                std::wstring unitWide(unitUtf8.begin(), unitUtf8.end());
                ListView_SetItemText(m_listView, i, 3, (LPWSTR)unitWide.c_str());
            }
        }
    }

    bool IsVisible() const {
        return m_hWnd && IsWindow(m_hWnd) && IsWindowVisible(m_hWnd);
    }

    void SetTargetVisible(bool visible) {
        m_targetVisible = visible;
    }

    bool IsTargetVisible() const {
        return m_targetVisible;
    }

    void SetOwner(const std::string& id) {
        std::lock_guard<std::mutex> lock(m_ownerMutex);
        m_ownerId = id;
    }

    bool IsOwner(const std::string& id) const {
        std::lock_guard<std::mutex> lock(m_ownerMutex);
        return m_ownerId == id;
    }

private:
    ToolParamListWindow() = default;
    ~ToolParamListWindow() { Close(); }

    HWND m_hWnd = nullptr;
    HWND m_listView = nullptr;
    std::shared_ptr<IAudioPluginHost> m_host;
    std::atomic<bool> m_targetVisible{ false };
    std::string m_ownerId;
    mutable std::mutex m_ownerMutex;

    static LRESULT CALLBACK WndProc(HWND hWnd, uint32_t msg, WPARAM wp, LPARAM lp) {
        ToolParamListWindow* self = nullptr;
        if (msg == WM_CREATE) {
            CREATESTRUCT* cs = (CREATESTRUCT*)lp;
            self = (ToolParamListWindow*)cs->lpCreateParams;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)self);
            self->m_hWnd = hWnd;
            self->CreateControls(hWnd);
        }
        else {
            self = (ToolParamListWindow*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        }

        switch (msg) {
        case WM_SIZE:
            if (self) self->ResizeControls(LOWORD(lp), HIWORD(lp));
            break;
        case WM_CLOSE:
            DestroyWindow(hWnd);
            break;
        case WM_DESTROY:
            if (self) {
                self->m_hWnd = nullptr;
                self->m_listView = nullptr;
            }
            break;
        default:
            return DefWindowProc(hWnd, msg, wp, lp);
        }
        return 0;
    }

    void CreateControls(HWND parent) {
        RECT rc;
        GetClientRect(parent, &rc);

        m_listView = CreateWindowEx(0, WC_LISTVIEW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
            0, 0, rc.right, rc.bottom,
            parent, NULL, GetModuleHandle(NULL), NULL);

        ListView_SetExtendedListViewStyle(m_listView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        LVCOLUMN lvc;
        lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;

        lvc.iSubItem = 0;
        lvc.pszText = (LPWSTR)L"Index";
        lvc.cx = 60;
        lvc.fmt = LVCFMT_LEFT;
        ListView_InsertColumn(m_listView, 0, &lvc);

        lvc.iSubItem = 1;
        lvc.pszText = (LPWSTR)L"Name";
        lvc.cx = 200;
        ListView_InsertColumn(m_listView, 1, &lvc);

        lvc.iSubItem = 2;
        lvc.pszText = (LPWSTR)L"Step";
        lvc.cx = 80;
        ListView_InsertColumn(m_listView, 2, &lvc);

        lvc.iSubItem = 3;
        lvc.pszText = (LPWSTR)L"Unit";
        lvc.cx = 80;
        ListView_InsertColumn(m_listView, 3, &lvc);
    }

    void ResizeControls(int32_t width, int32_t height) {
        if (m_listView) MoveWindow(m_listView, 0, 0, width, height, TRUE);
    }
};
