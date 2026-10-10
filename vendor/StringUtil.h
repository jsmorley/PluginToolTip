// Copyright (c) Rainmeter Team. GPL v2. Extracted case helpers only.
#pragma once
#include <Windows.h>
#include <cwctype>
namespace StringUtil {
inline void ToLowerCase(WCHAR* str, size_t count) {
    LCMapString(LOCALE_USER_DEFAULT, LCMAP_LOWERCASE, str, (int)count, str, (int)count);
}
inline void ToUpperCase(WCHAR* str, size_t count) {
    LCMapString(LOCALE_USER_DEFAULT, LCMAP_UPPERCASE, str, (int)count, str, (int)count);
}
inline void ToProperCase(WCHAR* str, size_t count) {
    LCMapString(LOCALE_USER_DEFAULT, LCMAP_TITLECASE, str, (int)count, str, (int)count);
}
inline void ToSentenceCase(WCHAR* str, size_t count) {
    ToLowerCase(str, count);
    bool capped = false;
    for (size_t i = 0; i < count; ++i) {
        if (str[i] == L'?' || str[i] == L'!' || str[i] == L'.') capped = false;
        if (!capped && iswalpha(str[i])) {
            ToUpperCase(str + i, 1);
            capped = true;
        }
    }
}
}
