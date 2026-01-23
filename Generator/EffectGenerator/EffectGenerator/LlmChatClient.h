// LlmChatClient.h
#pragma once
#include "ILlmTransport.h"
#include <memory>
#include <atomic>

class LlmChatClient {
public:
    using OnTokenW = std::function<void(const std::wstring&)>;
    using OnLogW = std::function<void(const std::wstring&)>;

    explicit LlmChatClient(std::unique_ptr<ILlmTransport> transport);

    bool Connect(const WsEndpoint& ep, OnLogW onLog);
    void Disconnect();
    bool IsConnected() const;

    bool SendUserText(const std::wstring& userTextW, OnTokenW onToken, OnLogW onLog);

private:
    static std::string BuildChatJson(const std::string& userUtf8);
    static std::string ExtractTokenFromJsonLoose(const std::string& jsonUtf8);

private:
    std::unique_ptr<ILlmTransport> m_transport;
    std::atomic<bool> m_cancel{ false };

    OnTokenW m_onToken;
    OnLogW   m_onLog;
};
