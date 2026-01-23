// Win32Util.h
#pragma once
#include <windows.h>
#include <string>

namespace Win32Util {
    std::string  WideToUtf8(const std::wstring& w);
    std::wstring Utf8ToWide(const std::string& s);

    void AppendTextToEdit(HWND hEdit, const std::wstring& text);
    std::wstring GetEditText(HWND hEdit);
    void SetEditText(HWND hEdit, const std::wstring& text);
}
