// App.h
#pragma once
#include <windows.h>
#include <string>
#include <memory>
#include "LlmChatClient.h"

class App {
public:
    static constexpr LRESULT kNotHandled = (LRESULT)0x7fffffff;

    static void CreateSingleton(HWND hWnd);
    static App* Get();

    explicit App(HWND hWnd);
    ~App();

    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

private:
    void OnCreate();
    void OnSize(int w, int h);
    void OnCommand(WPARAM wp, LPARAM lp);
    void OnDestroy();

    void UiLog(const std::wstring& s);
    void UiToken(const std::wstring& s);

    void DoConnect();
    void DoSend();

private:
    HWND m_hWnd = nullptr;
    HWND m_hEditLog = nullptr;
    HWND m_hEditInput = nullptr;
    HWND m_hBtnConnect = nullptr;
    HWND m_hBtnSend = nullptr;

    std::unique_ptr<LlmChatClient> m_client;

    static constexpr UINT WM_APP_LOG = WM_APP + 10;
    static constexpr UINT WM_APP_TOKEN = WM_APP + 11;
};
