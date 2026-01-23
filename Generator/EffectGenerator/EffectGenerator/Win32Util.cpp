// Win32Util.cpp
#include "Win32Util.h"

namespace Win32Util {

    std::string WideToUtf8(const std::wstring& w) {
        if (w.empty()) return {};

        const int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
            nullptr, 0, nullptr, nullptr);
        std::string out;
        out.resize(len);

        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
            &out[0], len, nullptr, nullptr);
        return out;
    }

    std::wstring Utf8ToWide(const std::string& s) {
        if (s.empty()) return {};

        const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(),
            nullptr, 0);
        std::wstring out;
        out.resize(len);

        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(),
            &out[0], len);
        return out;
    }

    void AppendTextToEdit(HWND hEdit, const std::wstring& text) {
        if (!hEdit) return;
        const int len = GetWindowTextLengthW(hEdit);
        SendMessageW(hEdit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
        SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
        SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);
    }

    std::wstring GetEditText(HWND hEdit) {
        if (!hEdit) return {};

        const int len = GetWindowTextLengthW(hEdit);
        std::wstring buf;
        buf.resize(len);

        if (len > 0) {
            GetWindowTextW(hEdit, &buf[0], len + 1);
        }
        return buf;
    }

    void SetEditText(HWND hEdit, const std::wstring& text) {
        if (!hEdit) return;
        SetWindowTextW(hEdit, text.c_str());
    }
    \

} // namespace
