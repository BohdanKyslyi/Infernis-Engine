//////////////////////////////////////////////////////////////////////////
// string_table.h:		таблица строк используемых в игре
//////////////////////////////////////////////////////////////////////////

#pragma once

#include "string_table_defs.h"

using STRING_TABLE_MAP = xr_map<STRING_ID, STRING_VALUE>;
using STRING_ID_SET = xr_set<STRING_ID>;
using STRING_LANGUAGE_COUNTS = xr_map<shared_str, u32>;

enum class EStringTableEncoding : u8 {
    Windows1251,
    Utf8,
    Mixed,
};

struct STRING_TABLE_DATA {
    shared_str m_sLanguage;
    xr_vector<shared_str> m_FallbackLanguages;
    EStringTableEncoding m_SourceEncoding = EStringTableEncoding::Windows1251;

    STRING_TABLE_MAP m_StringTable;
    STRING_TABLE_MAP m_string_key_binding;

    STRING_LANGUAGE_COUNTS m_LanguageStringCounts;
    mutable STRING_ID_SET m_MissingStringIds;
    u32 m_uFilesLoaded = 0;
    u32 m_uDuplicateIds = 0;
    u32 m_uEmptyEntries = 0;
    u32 m_uFallbackStrings = 0;
};

class CStringTable {
public:
    CStringTable();

    static void Destroy();
    static void DumpDiagnostics();

    STRING_VALUE translate(const STRING_ID& str_id) const;
    void rescan();

    static void ReparseKeyBindings();

private:
    void Init();
    void LoadLanguage(LPCSTR language, bool is_fallback);
    void Load(LPCSTR language, LPCSTR xml_file, bool is_fallback,
              STRING_ID_SET& language_ids, STRING_ID_SET& loaded_language_ids);
    static STRING_VALUE ParseLine(LPCSTR str, LPCSTR key, bool bFirst);
    static STRING_TABLE_DATA* pData;
};
