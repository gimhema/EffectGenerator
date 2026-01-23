// LlmChatClient.cpp
#include "LlmChatClient.h"
#include "Win32Util.h"

static std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"':  out += "\\\""; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:   out += c;      break;
        }
    }
    return out;
}

LlmChatClient::LlmChatClient(std::unique_ptr<ILlmTransport> transport)
    : m_transport(std::move(transport)) {
}

bool LlmChatClient::Connect(const WsEndpoint& ep, OnLogW onLog) {
    m_onLog = onLog;

    auto onText = [this](const std::string& utf8) {
        // 서버가 보내는 JSON에서 토큰만 추출해서 UI로 전달
        const std::string tok = ExtractTokenFromJsonLoose(utf8);
        if (!tok.empty() && m_onToken) {
            m_onToken(Win32Util::Utf8ToWide(tok));
        }
        else {
            // 토큰이 아니라면 원문 로그로 찍고 싶으면 여기서 찍어도 됨
            if (m_onLog) m_onLog(L"[NET] " + Win32Util::Utf8ToWide(utf8) + L"\r\n");
        }
        };

    auto onLogT = [this](const std::wstring& w) {
        if (m_onLog) m_onLog(w);
        };

    return m_transport->Connect(ep, onText, onLogT);
}

void LlmChatClient::Disconnect() {
    m_transport->Disconnect();
}

bool LlmChatClient::IsConnected() const {
    return m_transport->IsConnected();
}

bool LlmChatClient::SendUserText(const std::wstring& userTextW, OnTokenW onToken, OnLogW onLog) {
    m_onToken = onToken;
    m_onLog = onLog;

    if (!IsConnected()) {
        if (m_onLog) m_onLog(L"[SYS] Not connected.\r\n");
        return false;
    }

    const std::string userUtf8 = Win32Util::WideToUtf8(userTextW);
    const std::string body = BuildChatJson(userUtf8);

    if (m_onLog) m_onLog(L"[NET] -> send chat json\r\n");
    return m_transport->SendText(body);
}

std::string LlmChatClient::BuildChatJson(const std::string& userUtf8) {
    // 서버에 맞춰 최소 형태. 필요하면 여기서 history/sessionId 등 추가.
    // {"type":"chat","text":"..."}
    std::string s;
    s += "{";
    s += "\"type\":\"chat\",";
    s += "\"text\":\"" + JsonEscape(userUtf8) + "\"";
    s += "}";
    return s;
}

// 매우 느슨한 토큰 추출:
// 1) "content":"..."
// 2) "token":"..."
// 3) "text":"..."
static std::string ExtractStringField(const std::string& json, const char* key) {
    std::string k = std::string("\"") + key + "\":\"";
    auto pos = json.find(k);
    if (pos == std::string::npos) return {};
    pos += k.size();
    std::string val;
    while (pos < json.size()) {
        char c = json[pos++];
        if (c == '"') break;
        if (c == '\\' && pos < json.size()) {
            char n = json[pos++];
            if (n == 'n') val.push_back('\n');
            else if (n == 'r') val.push_back('\r');
            else if (n == 't') val.push_back('\t');
            else val.push_back(n);
        }
        else {
            val.push_back(c);
        }
    }
    return val;
}

std::string LlmChatClient::ExtractTokenFromJsonLoose(const std::string& jsonUtf8) {
    // done 신호
    if (jsonUtf8.find("\"type\":\"done\"") != std::string::npos) return {};

    // OpenAI 스타일 delta.content
    {
        auto v = ExtractStringField(jsonUtf8, "content");
        if (!v.empty()) return v;
    }
    {
        auto v = ExtractStringField(jsonUtf8, "token");
        if (!v.empty()) return v;
    }
    {
        auto v = ExtractStringField(jsonUtf8, "text");
        if (!v.empty()) return v;
    }
    return {};
}
