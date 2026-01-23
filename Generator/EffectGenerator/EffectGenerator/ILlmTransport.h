// ILlmTransport.h
#pragma once
#include <string>
#include <functional>

struct WsEndpoint {
    std::wstring host = L"127.0.0.1";
    int port = 8080;
    std::wstring path = L"/ws";   // 서버 WebSocket 경로
    bool useTls = false;          // wss 사용 시 true
};

class ILlmTransport {
public:
    using OnTextFn = std::function<void(const std::string& utf8Text)>; // raw message
    using OnLogFn = std::function<void(const std::wstring&)>;

    virtual ~ILlmTransport() = default;

    virtual bool Connect(const WsEndpoint& ep, OnTextFn onText, OnLogFn onLog) = 0;
    virtual void Disconnect() = 0;
    virtual bool IsConnected() const = 0;

    virtual bool SendText(const std::string& utf8Text) = 0;
};
