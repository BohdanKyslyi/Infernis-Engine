#include "stdafx.h"
#include "string_table.h"

#include "ui/xrUIXmlParser.h"
#include "xr_level_controller.h"

namespace {
constexpr UINT LEGACY_STRING_TABLE_CODE_PAGE = 1251;

bool LocalizationDevMode() {
    static const bool enabled = strstr(Core.Params, "-dev_mode") != nullptr;
    return enabled;
}

xr_string Utf8ToLegacyString(LPCSTR text, LPCSTR xml_file, LPCSTR string_id) {
    if (!text || !text[0])
        return text ? text : "";

    const unsigned char* byte = reinterpret_cast<const unsigned char*>(text);
    while (*byte && *byte < 0x80)
        ++byte;
    if (!*byte)
        return text;

    const int wide_size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    if (wide_size <= 0) {
        R_ASSERT4(false, "Invalid UTF-8 string table entry", xml_file, string_id);
        return text;
    }

    xr_vector<wchar_t> wide_text(wide_size);
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide_text.data(),
                            wide_size) != wide_size) {
        R_ASSERT4(false, "Can't decode UTF-8 string table entry", xml_file, string_id);
        return text;
    }

    BOOL used_default_character = FALSE;
    const int legacy_size = WideCharToMultiByte(
        LEGACY_STRING_TABLE_CODE_PAGE, WC_NO_BEST_FIT_CHARS, wide_text.data(), wide_size,
        nullptr, 0, "?", &used_default_character);
    if (legacy_size <= 0) {
        R_ASSERT4(false, "Can't convert UTF-8 string table entry to Windows-1251",
                  xml_file, string_id);
        return text;
    }

    xr_vector<char> legacy_text(legacy_size);
    if (WideCharToMultiByte(LEGACY_STRING_TABLE_CODE_PAGE, WC_NO_BEST_FIT_CHARS,
                            wide_text.data(), wide_size, legacy_text.data(), legacy_size, "?",
                            &used_default_character) != legacy_size) {
        R_ASSERT4(false, "Can't encode string table entry as Windows-1251", xml_file,
                  string_id);
        return text;
    }

    if (used_default_character) {
        Msg("! [string table] '%s' in '%s' contains characters missing from Windows-1251",
            string_id, xml_file);
    }

    return xr_string(legacy_text.data());
}

bool IsValidUtf8(const u8* data, size_t size) {
    size_t i = 0;
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
        i = 3;

    while (i < size) {
        const u8 first = data[i];
        if (first <= 0x7F) {
            ++i;
            continue;
        }

        if (first >= 0xC2 && first <= 0xDF) {
            if (i + 1 >= size || (data[i + 1] & 0xC0) != 0x80)
                return false;
            i += 2;
            continue;
        }

        if (first >= 0xE0 && first <= 0xEF) {
            if (i + 2 >= size || (data[i + 1] & 0xC0) != 0x80 ||
                (data[i + 2] & 0xC0) != 0x80)
                return false;
            if ((first == 0xE0 && data[i + 1] < 0xA0) ||
                (first == 0xED && data[i + 1] >= 0xA0))
                return false;
            i += 3;
            continue;
        }

        if (first >= 0xF0 && first <= 0xF4) {
            if (i + 3 >= size || (data[i + 1] & 0xC0) != 0x80 ||
                (data[i + 2] & 0xC0) != 0x80 || (data[i + 3] & 0xC0) != 0x80)
                return false;
            if ((first == 0xF0 && data[i + 1] < 0x90) ||
                (first == 0xF4 && data[i + 1] > 0x8F))
                return false;
            i += 4;
            continue;
        }

        return false;
    }

    return true;
}

LPCSTR StringTableEncodingName(EStringTableEncoding encoding) {
    switch (encoding) {
    case EStringTableEncoding::Windows1251:
        return "Windows-1251";
    case EStringTableEncoding::Utf8:
        return "UTF-8";
    case EStringTableEncoding::Mixed:
        return "mixed UTF-8/Windows-1251";
    }

    return "unknown";
}

EStringTableEncoding ReadStringTableEncoding() {
    if (!pSettings->line_exist("string_table", "encoding"))
        return EStringTableEncoding::Windows1251;

    LPCSTR encoding = pSettings->r_string("string_table", "encoding");
    if (!_stricmp(encoding, "utf-8") || !_stricmp(encoding, "utf8"))
        return EStringTableEncoding::Utf8;

    if (!_stricmp(encoding, "windows-1251") || !_stricmp(encoding, "cp1251"))
        return EStringTableEncoding::Windows1251;

    if (!_stricmp(encoding, "mixed") ||
        !_stricmp(encoding, "utf-8+windows-1251") ||
        !_stricmp(encoding, "utf8+cp1251"))
        return EStringTableEncoding::Mixed;

    R_ASSERT3(false, "Unsupported string table encoding", encoding);
    return EStringTableEncoding::Windows1251;
}

bool DetectUtf8File(LPCSTR path, LPCSTR xml_file) {
    string_path relative_path;
    strconcat(sizeof(relative_path), relative_path, path, "\\", xml_file);

    IReader* reader = FS.r_open(CONFIG_PATH, relative_path);
    R_ASSERT3(reader, "Can't inspect string table encoding", relative_path);
    if (!reader)
        return false;

    const size_t size = reader->length();
    if (!size) {
        FS.r_close(reader);
        R_ASSERT3(false, "Empty string table XML file", relative_path);
        return false;
    }

    const u8* data = static_cast<const u8*>(reader->pointer());

    bool is_utf8 = false;
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        is_utf8 = true;
    } else {
        constexpr size_t XML_HEADER_LIMIT = 512;
        char header[XML_HEADER_LIMIT + 1] = {};
        const size_t header_size = std::min(size, XML_HEADER_LIMIT);
        memcpy(header, data, header_size);

        for (size_t i = 0; i < header_size; ++i) {
            if (header[i] >= 'A' && header[i] <= 'Z')
                header[i] += 'a' - 'A';
        }

        char* declaration = strstr(header, "<?xml");
        char* declaration_end = declaration ? strstr(declaration, "?>") : nullptr;
        if (declaration_end)
            *declaration_end = '\0';

        const bool has_encoding = declaration && strstr(declaration, "encoding");
        if (has_encoding &&
            (strstr(declaration, "utf-8") || strstr(declaration, "utf8"))) {
            is_utf8 = true;
        } else if (has_encoding &&
                   (strstr(declaration, "windows-1251") ||
                    strstr(declaration, "windows1251") || strstr(declaration, "cp1251"))) {
            is_utf8 = false;
        } else if (has_encoding) {
            R_ASSERT3(false, "Unsupported XML string table encoding", xml_file);
            is_utf8 = false;
        } else {
            is_utf8 = IsValidUtf8(data, size);
        }
    }

    FS.r_close(reader);
    return is_utf8;
}

bool IsUtf8File(EStringTableEncoding mode, LPCSTR path, LPCSTR xml_file) {
    switch (mode) {
    case EStringTableEncoding::Windows1251:
        return false;
    case EStringTableEncoding::Utf8:
        return true;
    case EStringTableEncoding::Mixed: {
        const bool is_utf8 = DetectUtf8File(path, xml_file);
        if (LocalizationDevMode())
            Msg("* [string table] '%s\\%s' detected as %s", path, xml_file,
                is_utf8 ? "UTF-8" : "Windows-1251");
        return is_utf8;
    }
    }

    return false;
}
} // namespace

STRING_TABLE_DATA* CStringTable::pData = NULL;

CStringTable::CStringTable() { Init(); }

void CStringTable::Destroy() { xr_delete(pData); }

void CStringTable::rescan() {
    STRING_TABLE_DATA* old_data = pData;
    pData = NULL;
    Init();
    xr_delete(old_data);

    if (LocalizationDevMode())
        Msg("* [string table] localization reloaded");
}

void CStringTable::Init() {
    if (NULL != pData)
        return;

    pData = xr_new<STRING_TABLE_DATA>();

    //имя языка, если не задано (NULL), то первый <text> в <string> в XML
    pData->m_sLanguage = pSettings->r_string("string_table", "language");
    pData->m_SourceEncoding = ReadStringTableEncoding();

    if (pSettings->line_exist("string_table", "fallback_language")) {
        LPCSTR fallback_languages = pSettings->r_string("string_table", "fallback_language");
        const int language_count = _GetItemCount(fallback_languages);

        for (int i = 0; i < language_count; ++i) {
            string256 language;
            _GetItem(fallback_languages, i, language);

            if (!language[0])
                continue;

            if (!_stricmp(language, pData->m_sLanguage.c_str())) {
                if (LocalizationDevMode())
                    Msg("! [string table] fallback language '%s' is also the primary language",
                        language);
                continue;
            }

            bool already_added = false;
            for (const shared_str& added_language : pData->m_FallbackLanguages) {
                if (!_stricmp(language, added_language.c_str())) {
                    already_added = true;
                    break;
                }
            }

            if (already_added) {
                if (LocalizationDevMode())
                    Msg("! [string table] duplicate fallback language '%s'", language);
                continue;
            }

            pData->m_FallbackLanguages.emplace_back(language);
        }
    }

    LoadLanguage(pData->m_sLanguage.c_str(), false);
    for (const shared_str& fallback_language : pData->m_FallbackLanguages)
        LoadLanguage(fallback_language.c_str(), true);

    ReparseKeyBindings();

    if (LocalizationDevMode())
        DumpDiagnostics();
}

void CStringTable::LoadLanguage(LPCSTR language, bool is_fallback) {
    FS_FileSet fset;
    string_path files_mask;
    xr_sprintf(files_mask, "text\\%s\\*.xml", language);
    FS.file_list(fset, "$game_config$", FS_ListFiles, files_mask);

    if (fset.empty() && LocalizationDevMode())
        Msg("! [string table] no XML files found for language '%s'", language);

    STRING_ID_SET language_ids;
    STRING_ID_SET loaded_language_ids;
    for (const FS_File& file : fset) {
        string_path fn, ext;
        _splitpath(file.name.c_str(), 0, 0, fn, ext);
        xr_strcat(fn, ext);

        Load(language, fn, is_fallback, language_ids, loaded_language_ids);
    }

    pData->m_uFilesLoaded += static_cast<u32>(fset.size());
}

void CStringTable::Load(LPCSTR language, LPCSTR xml_file_full, bool is_fallback,
                        STRING_ID_SET& language_ids, STRING_ID_SET& loaded_language_ids) {
    CUIXml uiXml;
    string_path _s;
    strconcat(sizeof(_s), _s, "text\\", language);
    const bool is_utf8 = IsUtf8File(pData->m_SourceEncoding, _s, xml_file_full);

    uiXml.Load(CONFIG_PATH, _s, xml_file_full);

    //общий список всех записей таблицы в файле
    int string_num = uiXml.GetNodesNum(uiXml.GetRoot(), "string");

    for (int i = 0; i < string_num; ++i) {
        LPCSTR string_name = uiXml.ReadAttrib(uiXml.GetRoot(), "string", i, "id", NULL);

        if (!string_name || !string_name[0]) {
            ++pData->m_uEmptyEntries;
            if (LocalizationDevMode())
                Msg("! [string table] entry without id in '%s\\%s'", language,
                    xml_file_full);
            continue;
        }

        LPCSTR string_text = uiXml.Read(uiXml.GetRoot(), "string:text", i, NULL);

        if (!string_text || !string_text[0]) {
            ++pData->m_uEmptyEntries;
            if (LocalizationDevMode())
                Msg("! [string table] empty text for id '%s' in '%s\\%s'", string_name,
                    language, xml_file_full);
            continue;
        }

        const bool duplicate = !language_ids.insert(string_name).second;
        if (duplicate) {
            ++pData->m_uDuplicateIds;
            if (LocalizationDevMode())
                Msg("! [string table] duplicate id '%s' in language '%s' (file '%s')",
                    string_name, language, xml_file_full);
        }

        const bool loaded_by_current_language =
            loaded_language_ids.find(string_name) != loaded_language_ids.end();
        if (is_fallback && !loaded_by_current_language &&
            pData->m_StringTable.find(string_name) != pData->m_StringTable.end())
            continue;

        const xr_string normalized_text = is_utf8
            ? Utf8ToLegacyString(string_text, xml_file_full, string_name)
            : xr_string(string_text);
        STRING_VALUE str_val = ParseLine(normalized_text.c_str(), string_name, true);

        pData->m_StringTable[string_name] = str_val;
        if (loaded_language_ids.insert(string_name).second) {
            ++pData->m_LanguageStringCounts[language];
            if (is_fallback)
                ++pData->m_uFallbackStrings;
        }
    }
}

void CStringTable::DumpDiagnostics() {
    if (!pData)
        return;

    Msg("* [string table] diagnostics");
    Msg("* [string table] primary language: %s", pData->m_sLanguage.c_str());

    if (pData->m_FallbackLanguages.empty()) {
        Msg("* [string table] fallback languages: none");
    } else {
        xr_string fallback_list;
        for (const shared_str& language : pData->m_FallbackLanguages) {
            if (!fallback_list.empty())
                fallback_list += ", ";
            fallback_list += language.c_str();
        }
        Msg("* [string table] fallback languages: %s", fallback_list.c_str());
    }

    Msg("* [string table] source encoding: %s",
        StringTableEncodingName(pData->m_SourceEncoding));
    Msg("* [string table] loaded: %u files, %u strings (%u from fallback)",
        pData->m_uFilesLoaded, static_cast<u32>(pData->m_StringTable.size()),
        pData->m_uFallbackStrings);

    for (const auto& language_count : pData->m_LanguageStringCounts)
        Msg("* [string table] language '%s': %u strings",
            language_count.first.c_str(), language_count.second);

    Msg("* [string table] problems: %u duplicate ids, %u empty entries, %u missing ids",
        pData->m_uDuplicateIds, pData->m_uEmptyEntries,
        static_cast<u32>(pData->m_MissingStringIds.size()));

    for (const STRING_ID& missing_id : pData->m_MissingStringIds)
        Msg("! [string table] unresolved id: '%s'", missing_id.c_str());
}

void CStringTable::ReparseKeyBindings() {
    if (!pData)
        return;
    auto it = pData->m_string_key_binding.begin();
    auto it_e = pData->m_string_key_binding.end();

    for (; it != it_e; ++it) {
        pData->m_StringTable[it->first] = ParseLine(*it->second, *it->first, false);
    }
}

STRING_VALUE CStringTable::ParseLine(LPCSTR str, LPCSTR skey, bool bFirst) {
    //	LPCSTR str = "1 $$action_left$$ 2 $$action_right$$ 3 $$action_left$$ 4";
    xr_string res;
    int k = 0;
    const char* b;
#define ACTION_STR "$$ACTION_"

//.	int LEN				= (int)xr_strlen(ACTION_STR);
#define LEN 9

    string256 buff;
    string256 srcbuff;
    bool b_hit = false;

    while ((b = strstr(str + k, ACTION_STR)) != 0) {
        buff[0] = 0;
        srcbuff[0] = 0;
        res.append(str + k, b - str - k);
        const char* e = strstr(b + LEN, "$$");

        int len = (int)(e - b - LEN);

        strncpy_s(srcbuff, b + LEN, len);
        srcbuff[len] = 0;
        GetActionAllBinding(srcbuff, buff, sizeof(buff));
        res.append(buff, xr_strlen(buff));

        k = (int)(b - str);
        k += len;
        k += LEN;
        k += 2;
        b_hit = true;
    };

    if (k < (int)xr_strlen(str)) {
        res.append(str + k);
    }

    if (b_hit && bFirst)
        pData->m_string_key_binding[skey] = str;

    return STRING_VALUE(res.c_str());
}

STRING_VALUE CStringTable::translate(const STRING_ID& str_id) const {
    VERIFY(pData);

    if (pData->m_StringTable.find(str_id) != pData->m_StringTable.end())
        return pData->m_StringTable[str_id];

    if (LocalizationDevMode() && pData->m_MissingStringIds.insert(str_id).second)
        Msg("! [string table] missing localization id '%s'", str_id.c_str());

    return str_id;
}
