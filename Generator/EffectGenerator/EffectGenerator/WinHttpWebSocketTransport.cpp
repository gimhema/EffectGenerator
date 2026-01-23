// WinHttpWebSocketTransport.cpp
#include "WinHttpWebSocketTransport.h"
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")


static std::wstring W(const wchar_t* s) { return std::wstring(s); }

WinHttpWebSocketTransport::WinHttpWebSocketTransport() {}
WinHttpWebSocketTransport::~WinHttpWebSocketTransport() { Disconnect(); }

void WinHttpWebSocketTransport::Log(const std::wstring& s) {
    if (m_onLog) m_onLog(s);
}

static std::wstring PortToStr(int port) {
    return std::to_wstring(port);
}

// 16바이트 랜덤 → Base64 (Sec-WebSocket-Key)
std::wstring WinHttpWebSocketTransport::GenSecWebSocketKeyBase64() {
    unsigned char rnd[16]{};
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_RNG_ALGORITHM, nullptr, 0) == 0) {
        BCryptGenRandom(hAlg, rnd, (ULONG)sizeof(rnd), 0);
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
    else {
        for (int i = 0; i < 16; ++i) rnd[i] = (unsigned char)(GetTickCount() >> (i % 8));
    }

    DWORD outLen = 0;
    CryptBinaryToStringW(rnd, (DWORD)sizeof(rnd),
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &outLen);

    std::wstring out;
    out.resize(outLen);

    CryptBinaryToStringW(rnd, (DWORD)sizeof(rnd),
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &out[0], &outLen);

    // outLen 기반으로 정리 (널/개행 제거)
    while (!out.empty() && (out.back() == L'\0' || out.back() == L'\r' || out.back() == L'\n'))
        out.pop_back();

    return out;
}


std::wstring WinHttpWebSocketTransport::BuildWsHeaders() {
    std::wstring key = GenSecWebSocketKeyBase64();
    std::wstring h;
    h += L"Connection: Upgrade\r\n";
    h += L"Upgrade: websocket\r\n";
    h += L"Sec-WebSocket-Version: 13\r\n";
    h += L"Sec-WebSocket-Key: " + key + L"\r\n";
    return h;
}

bool WinHttpWebSocketTransport::Connect(const WsEndpoint& ep, OnTextFn onText, OnLogFn onLog) {
    Disconnect();

    m_ep = ep;
    m_onText = onText;
    m_onLog = onLog;
    m_stop = false;

    Log(L"[NET] Connecting...\r\n");

    m_hSession = WinHttpOpen(L"EffectGeneratorWsClient/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);

    if (!m_hSession) {
        Log(L"[NET] WinHttpOpen failed.\r\n");
        return false;
    }

    m_hConnect = WinHttpConnect(m_hSession, m_ep.host.c_str(), (INTERNET_PORT)m_ep.port, 0);
    if (!m_hConnect) {
        Log(L"[NET] WinHttpConnect failed.\r\n");
        Disconnect();
        return false;
    }

    DWORD flags = m_ep.useTls ? WINHTTP_FLAG_SECURE : 0;
    m_hRequest = WinHttpOpenRequest(
        m_hConnect,
        L"GET",
        m_ep.path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags
    );

    if (!m_hRequest) {
        Log(L"[NET] WinHttpOpenRequest failed.\r\n");
        Disconnect();
        return false;
    }

    if (!WinHttpSetOption(m_hRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
        Log(L"[NET] WinHttpSetOption(UPGRADE_TO_WEB_SOCKET) failed.\r\n");
        Disconnect();
        return false;
    }

    // (선택) 서버에서 헤더 기반 체크를 한다면 Host 등을 추가할 수 있음.
    // 일반적으로는 추가 헤더 없이도 충분.
    BOOL ok = WinHttpSendRequest(
        m_hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0,
        0, 0
    );

    if (!ok) {
        Log(L"[NET] WinHttpSendRequest failed.\r\n");
        Disconnect();
        return false;
    }

    ok = WinHttpReceiveResponse(m_hRequest, nullptr);
    if (!ok) {
        Log(L"[NET] WinHttpReceiveResponse failed.\r\n");
        Disconnect();
        return false;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (WinHttpQueryHeaders(m_hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &status, &statusSize, WINHTTP_NO_HEADER_INDEX))
    {
        wchar_t buf[128];
        swprintf_s(buf, L"[NET] HTTP status: %lu\r\n", status);
        Log(buf);
    }

    m_hWebSock = WinHttpWebSocketCompleteUpgrade(m_hRequest, 0);
    if (!m_hWebSock) {
        Log(L"[NET] WebSocket upgrade failed.\r\n");
        Disconnect();
        return false;
    }

    // Request handle은 upgrade 후 닫아도 됨
    WinHttpCloseHandle(m_hRequest);
    m_hRequest = nullptr;

    m_connected = true;
    Log(L"[NET] Connected (WebSocket).\r\n");

    m_recvThread = std::thread(&WinHttpWebSocketTransport::RecvLoop, this);
    return true;
}


void WinHttpWebSocketTransport::Disconnect() {
    m_stop = true;

    if (m_connected.load() && m_hWebSock) {
        WinHttpWebSocketClose(m_hWebSock, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
    }

    if (m_recvThread.joinable())
        m_recvThread.join();

    m_connected = false;

    if (m_hWebSock) { WinHttpCloseHandle(m_hWebSock); m_hWebSock = nullptr; }
    if (m_hRequest) { WinHttpCloseHandle(m_hRequest); m_hRequest = nullptr; }
    if (m_hConnect) { WinHttpCloseHandle(m_hConnect); m_hConnect = nullptr; }
    if (m_hSession) { WinHttpCloseHandle(m_hSession); m_hSession = nullptr; }
}

bool WinHttpWebSocketTransport::SendText(const std::string& utf8Text) {
    if (!m_connected.load() || !m_hWebSock) return false;

    DWORD res = WinHttpWebSocketSend(
        m_hWebSock,
        WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
        (PVOID)utf8Text.data(),
        (DWORD)utf8Text.size()
    );

    return (res == NO_ERROR);
}

void WinHttpWebSocketTransport::RecvLoop() {
    std::vector<char> buf(64 * 1024);

    while (!m_stop.load() && m_hWebSock) {
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type = WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
        DWORD bytesRead = 0;

        DWORD res = WinHttpWebSocketReceive(
            m_hWebSock,
            buf.data(),
            (DWORD)buf.size(),
            &bytesRead,
            &type
        );

        if (res != NO_ERROR) {
            Log(L"[NET] Receive error / disconnected.\r\n");
            break;
        }

        if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
            Log(L"[NET] Server closed.\r\n");
            break;
        }

        if (bytesRead == 0) continue;

        std::string msg(buf.data(), buf.data() + bytesRead);

        if (m_onText) m_onText(msg);
    }

    m_connected = false;
}
