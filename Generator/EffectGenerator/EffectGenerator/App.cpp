// App.cpp
#include "App.h"
#include "Win32Util.h"
#include "WinHttpWebSocketTransport.h"

static App* g_app = nullptr;

static const int ID_BTN_CONNECT = 2001;
static const int ID_BTN_SEND = 2002;
static const int ID_EDIT_LOG = 2003;
static const int ID_EDIT_IN = 2004;

// x64에서 int -> HMENU 캐스팅 경고(C4312) 방지
static HMENU ToMenuId(int id) {
    return reinterpret_cast<HMENU>(static_cast<intptr_t>(id));
}

App* App::Get() { return g_app; }
void App::CreateSingleton(HWND hWnd) { if (!g_app) g_app = new App(hWnd); }

App::App(HWND hWnd) : m_hWnd(hWnd) {
    m_client = std::make_unique<LlmChatClient>(std::make_unique<WinHttpWebSocketTransport>());
}

App::~App() {
    if (m_client) m_client->Disconnect();
}

LRESULT App::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        OnCreate();
        return 0;

    case WM_SIZE:
        OnSize(LOWORD(lp), HIWORD(lp));
        return 0;

    case WM_COMMAND:
        OnCommand(wp, lp);
        return 0;

    case WM_DESTROY:
        OnDestroy();
        return 0;

    case WM_APP_LOG: {
        auto p = reinterpret_cast<std::wstring*>(lp);
        if (p) { UiLog(*p); delete p; }
        return 0;
    }

    case WM_APP_TOKEN: {
        auto p = reinterpret_cast<std::wstring*>(lp);
        if (p) { UiToken(*p); delete p; }
        return 0;
    }

    default:
        return kNotHandled;
    }
}

void App::OnCreate() {
    // Log 영역
    m_hEditLog = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        10, 10, 940, 520,
        m_hWnd,
        ToMenuId(ID_EDIT_LOG),
        GetModuleHandleW(nullptr),
        nullptr
    );

    // 입력 영역
    m_hEditInput = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
        10, 540, 760, 120,
        m_hWnd,
        ToMenuId(ID_EDIT_IN),
        GetModuleHandleW(nullptr),
        nullptr
    );

    // Connect 버튼
    m_hBtnConnect = CreateWindowW(
        L"BUTTON",
        L"Connect (WS)",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        780, 540, 170, 36,
        m_hWnd,
        ToMenuId(ID_BTN_CONNECT),
        GetModuleHandleW(nullptr),
        nullptr
    );

    // Send 버튼
    m_hBtnSend = CreateWindowW(
        L"BUTTON",
        L"Send",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        780, 584, 170, 36,
        m_hWnd,
        ToMenuId(ID_BTN_SEND),
        GetModuleHandleW(nullptr),
        nullptr
    );

    UiLog(L"[SYS] Ready.\r\n");
    UiLog(L"[SYS] Click Connect.\r\n");
}

void App::OnSize(int w, int h) {
    const int pad = 10;
    const int btnW = 170;
    const int btnH = 36;
    const int inputH = 140;

    int logW = w - pad * 2;
    int logH = h - pad * 3 - inputH;
    if (logW < 10) logW = 10;
    if (logH < 10) logH = 10;

    MoveWindow(m_hEditLog, pad, pad, logW, logH, TRUE);

    int inputY = pad * 2 + logH;
    int inputW = w - pad * 3 - btnW;
    if (inputW < 10) inputW = 10;

    MoveWindow(m_hEditInput, pad, inputY, inputW, inputH, TRUE);

    int btnX = pad * 2 + inputW;
    MoveWindow(m_hBtnConnect, btnX, inputY, btnW, btnH, TRUE);
    MoveWindow(m_hBtnSend, btnX, inputY + btnH + 8, btnW, btnH, TRUE);
}

void App::OnCommand(WPARAM wp, LPARAM) {
    int id = LOWORD(wp);

    if (id == ID_BTN_CONNECT) {
        DoConnect();
        return;
    }

    if (id == ID_BTN_SEND) {
        DoSend();
        return;
    }
}

void App::OnDestroy() {
    if (m_client) m_client->Disconnect();
    PostQuitMessage(0);

    delete g_app;
    g_app = nullptr;
}

void App::UiLog(const std::wstring& s) {
    Win32Util::AppendTextToEdit(m_hEditLog, s);
}

void App::UiToken(const std::wstring& s) {
    // 토큰은 Log에 그대로 이어붙임
    Win32Util::AppendTextToEdit(m_hEditLog, s);
}

void App::DoConnect() {
    WsEndpoint ep;
    ep.host = L"127.0.0.1";
    ep.port = 8080;     // TODO: 네 C# 서버 포트로 변경
    ep.path = L"/ws/";   // TODO: 네 C# 서버 WS 경로로 변경
    ep.useTls = false;

    auto postLog = [this](const std::wstring& s) {
        PostMessageW(m_hWnd, WM_APP_LOG, 0, (LPARAM)new std::wstring(s));
        };

    bool ok = m_client->Connect(ep, postLog);
    if (!ok) {
        UiLog(L"[SYS] Connect failed. Check port/path.\r\n");
    }
}

void App::DoSend() {
    if (!m_client->IsConnected()) {
        UiLog(L"[SYS] Not connected.\r\n");
        return;
    }

    std::wstring user = Win32Util::GetEditText(m_hEditInput);
    if (user.empty()) return;

    Win32Util::SetEditText(m_hEditInput, L"");

    UiLog(L"\r\nUser: ");
    UiLog(user);
    UiLog(L"\r\nAssistant: ");

    auto postToken = [this](const std::wstring& tok) {
        PostMessageW(m_hWnd, WM_APP_TOKEN, 0, (LPARAM)new std::wstring(tok));
        };

    auto postLog = [this](const std::wstring& s) {
        PostMessageW(m_hWnd, WM_APP_LOG, 0, (LPARAM)new std::wstring(s));
        };

    // WebSocket 수신은 RecvLoop가 백그라운드에서 수행.
    // SendUserText는 "요청 JSON"을 보내는 역할.
    m_client->SendUserText(user, postToken, postLog);
}
