// WinHttpWebSocketTransport.h
#pragma once
#include "ILlmTransport.h"
#include <windows.h>
#include <winhttp.h>
#include <thread>
#include <atomic>

#pragma comment(lib, "winhttp.lib")

class WinHttpWebSocketTransport : public ILlmTransport {
public:
    WinHttpWebSocketTransport();
    ~WinHttpWebSocketTransport() override;

    bool Connect(const WsEndpoint& ep, OnTextFn onText, OnLogFn onLog) override;
    void Disconnect() override;
    bool IsConnected() const override { return m_connected.load(); }

    bool SendText(const std::string& utf8Text) override;

private:
    void RecvLoop();
    void Log(const std::wstring& s);

    static std::wstring BuildWsHeaders(); // key µî Æ÷ÇÔ
    static std::wstring GenSecWebSocketKeyBase64();

private:
    WsEndpoint m_ep{};
    OnTextFn m_onText;
    OnLogFn  m_onLog;

    HINTERNET m_hSession = nullptr;
    HINTERNET m_hConnect = nullptr;
    HINTERNET m_hRequest = nullptr;
    HINTERNET m_hWebSock = nullptr;

    std::thread m_recvThread;
    std::atomic<bool> m_connected{ false };
    std::atomic<bool> m_stop{ false };
};
