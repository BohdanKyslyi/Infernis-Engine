#include "stdafx.h"
#pragma hdrstop

#include "xrXMLParser.h"

namespace {
// String tables contain display text. Accept literal '<' and '&' in their
// <text> elements while preserving CDATA and already escaped XML entities.

size_t XmlEntityLength(const char* at) {
    static const char* entities[] = {"&amp;", "&lt;", "&gt;", "&quot;", "&apos;"};
    for (const char* entity : entities) {
        const size_t size = xr_strlen(entity);
        if (strncmp(at, entity, size) == 0)
            return size;
    }

    if (at[1] != '#')
        return 0;

    const char* digit = at + 2;
    const bool hex = *digit == 'x' || *digit == 'X';
    if (hex)
        ++digit;
    const char* first = digit;
    while ((*digit >= '0' && *digit <= '9') ||
           (hex && ((*digit >= 'a' && *digit <= 'f') ||
                    (*digit >= 'A' && *digit <= 'F'))))
        ++digit;
    return digit != first && *digit == ';' ? digit + 1 - at : 0;
}

xr_string EscapeStringTableText(const char* xml) {
    xr_string escaped;
    const char* cursor = xml;
    while (const char* opening = strstr(cursor, "<text")) {
        // Other element names, such as <texture>, must not be treated as text.
		
        const char after_name = opening[5];
        if (after_name != '>' && after_name != ' ' && after_name != '\t' &&
            after_name != '\r' && after_name != '\n') {
            escaped.append(cursor, opening + 5);
            cursor = opening + 5;
            continue;
        }

        const char* body = strchr(opening, '>');
        if (!body)
            break;
        ++body;
        const char* closing = strstr(body, "</text>");
        if (!closing)
            break;

        escaped.append(cursor, body);
        for (const char* p = body; p < closing;) {
            if (closing - p >= 9 && strncmp(p, "<![CDATA[", 9) == 0) {
                const char* end = strstr(p + 9, "]]>");
                if (end && end < closing) {
                    escaped.append(p, end + 3);
                    p = end + 3;
                    continue;
                }
            }
            if (*p == '&') {
                const size_t entity_size = XmlEntityLength(p);
                if (entity_size && p + entity_size <= closing) {
                    escaped.append(p, p + entity_size);
                    p += entity_size;
                } else {
                    escaped += "&amp;";
                    ++p;
                }
            } else if (*p == '<') {
                escaped += "&lt;";
                ++p;
            } else {
                escaped += *p++;
            }
        }
        cursor = closing;
    }
    escaped += cursor;
    return escaped;
}
} // namespace

XRXMLPARSER_API CXml::CXml() : m_root(), m_pLocalRoot() {}

XRXMLPARSER_API CXml::~CXml() { ClearInternal(); }

void CXml::ClearInternal() { m_Doc.clear(); }

void ParseFile(const char* path, CMemoryWriter& W, IReader* F, CXml* xml) {
    string4096 str;

    while (!F->eof()) {
        F->r_string(str, sizeof(str));

        if (str[0] && (str[0] == '#') && strstr(str, "#include")) {
            string256 inc_name;
            if (_GetItem(str, 1, inc_name, '"')) {
                IReader* I = nullptr;
                if (inc_name == strstr(inc_name, "ui\\")) {
                    const auto fn = xml->correct_file_name("ui", strchr(inc_name, '\\') + 1);
                    string_path buff;
                    strconcat(sizeof(buff), buff, "ui\\", fn.c_str());
                    I = FS.r_open(path, buff);
                }

                if (!I)
                    I = FS.r_open(path, inc_name);

                if (!I) {
                    string1024 str;
                    xr_sprintf(str, "XML file[%s] parsing failed. Can't find include file:[%s]",
                               path, inc_name);
                    R_ASSERT2(false, str);
                }
                ParseFile(path, W, I, xml);
                FS.r_close(I);
            }
        } else
            W.w_string(str);
    }
}

void CXml::Load(const char* path_alias, const char* path, const char* _xml_filename) {
    const auto fn = correct_file_name(path, _xml_filename);

    string_path str;
    xr_sprintf(str, "%s\\%s", path, fn.c_str());
    return Load(path_alias, str);
}

bool CXml::TryLoad(const char* path_alias, const char* path, const char* xml_filename) {
    const auto fn = correct_file_name(path, xml_filename);
    string_path relative_path;
    xr_sprintf(relative_path, "%s\\%s", path, fn.c_str());
    return LoadInternal(path_alias, relative_path, true);
}

// Initialize and load an XML file
void CXml::Load(const char* path, const char* xml_filename) {
    LoadInternal(path, xml_filename, false);
}

bool CXml::LoadInternal(const char* path, const char* xml_filename, bool optional) {
    xr_strcpy(m_xml_file_name, xml_filename);
    // Load and parse xml file

    IReader* F = FS.r_open(path, xml_filename);
    if (!F) {
        // Refresh the directory once if a loose XML was installed after the FS scan.
		
        string_path full_path;
        FS.update_path(full_path, path, xml_filename);
        char* separator = strrchr(full_path, '\\');
        if (separator) {
            separator[1] = 0;
            Msg("! [XML] Cannot open %s; rescanning %s", xml_filename, full_path);
            FS.rescan_path(full_path, TRUE);
            F = FS.r_open(path, xml_filename);
        }
        if (!F) {
            FS.update_path(full_path, path, xml_filename);
            Msg("! [XML] Missing after rescan: %s", full_path);
            if (optional)
                return false;
            R_ASSERT2(F, full_path);
        }
    }

    CMemoryWriter W;
    ParseFile(path, W, F, this);
    W.w_stringZ("");
    FS.r_close(F);

    const char* xml_data = reinterpret_cast<const char*>(W.pointer());
    const xr_string normalized = optional ? EscapeStringTableText(xml_data) : xr_string();
    m_Doc.parse(optional ? normalized.c_str() : xml_data);
    if (m_Doc.isError()) {
        string1024 str;
        xr_sprintf(str, "XML file:%s errDescr:%s:%u", m_xml_file_name, m_Doc.error(), m_Doc.errorOffset());
        if (optional) {
            Msg("! [XML] Skipping malformed string table: %s", str);
            ClearInternal();
            m_root = XML_NODE();
            return false;
        }
        R_ASSERT2(false, str);
    }

    m_root = m_Doc.firstChildElement();
    return true;
}

XML_NODE CXml::NavigateToNode(XML_NODE start_node, const char* path, const size_t node_index) const {
    R_ASSERT3(start_node && path, "NavigateToNode failed in XML file ", m_xml_file_name);
    XML_NODE node;
    string_path buf_str;
    VERIFY(xr_strlen(path) < 200);
    buf_str[0] = 0;
    xr_strcpy(buf_str, path);

    const char seps[] = ":";
    size_t tmp = 0;

    // Split the path into components
	
    char* token = strtok(buf_str, seps);

    if (token != nullptr) {
        node = start_node.firstChild(token);

        while (tmp++ < node_index && node) {
            node = node.nextSibling(token);
        }
    }

    while (token) {
        // Get next token:
		
        token = strtok(nullptr, seps);

        if (token != nullptr)
            if (node) {
                const XML_NODE node_parent = node;
                node = node_parent.firstChild(token);
            }
    }

    return node;
}

XML_NODE CXml::NavigateToNode(const char* path, const size_t node_index) const {
    return NavigateToNode(GetLocalRoot() ? GetLocalRoot() : GetRoot(), path, node_index);
}

XML_NODE CXml::NavigateToNodeWithAttribute(const char* tag_name, const char* attrib_name,
                                           const char* attrib_value) {

    const XML_NODE root = GetLocalRoot() ? GetLocalRoot() : GetRoot();
    int tabsCount = GetNodesNum(root, tag_name);

    for (int i = 0; i < tabsCount; ++i) {
        const char* result = ReadAttrib(root, tag_name, i, attrib_name, "");
        if (result && xr_strcmp(result, attrib_value) == 0) {
            return NavigateToNode(root, tag_name, i);
        }
    }
    return XML_NODE();
}

const char* CXml::Read(const char* path, const size_t index, const char* default_str_val) const {
    const XML_NODE node = NavigateToNode(path, index);
    const char* result = Read(node, default_str_val);
    return result;
}

const char* CXml::Read(XML_NODE start_node, const char* path, const size_t index, const char* default_str_val) const {
    const XML_NODE node = NavigateToNode(start_node, path, index);
    const char* result = Read(node, default_str_val);
    return result;
}

const char* CXml::Read(XML_NODE node, const char* default_str_val) const {
    if (!node)
        return default_str_val;
    else {
        node = node.firstChild();
        if (!node)
            return default_str_val;

        return node.textValueOr(default_str_val);
    }
}

int CXml::ReadInt(XML_NODE node, const int default_int_val) const {
    const char* result_str = Read(node, nullptr);

    if (result_str == nullptr)
        return default_int_val;

    return atoi(result_str);
}

int CXml::ReadInt(const char* path, const size_t index, const int default_int_val) const {
    const char* result_str = Read(path, index, nullptr);
    if (result_str == nullptr)
        return default_int_val;

    return atoi(result_str);
}

int CXml::ReadInt(XML_NODE start_node, const char* path, const size_t index, const int default_int_val) const {
    const char* result_str = Read(start_node, path, index, nullptr);
    if (result_str == nullptr)
        return default_int_val;

    return atoi(result_str);
}

float CXml::ReadFlt(const char* path, const size_t index, const float default_flt_val) const {
    const char* result_str = Read(path, index, nullptr);
    if (result_str == nullptr)
        return default_flt_val;

    return static_cast<float>(atof(result_str));
}

float CXml::ReadFlt(XML_NODE start_node, const char* path, const size_t index, const float default_flt_val) const {
    const char* result_str = Read(start_node, path, index, nullptr);
    if (result_str == nullptr)
        return default_flt_val;

    return static_cast<float>(atof(result_str));
}

float CXml::ReadFlt(XML_NODE node, const float default_flt_val) const {
    const char* result_str = Read(node, nullptr);

    if (result_str == nullptr)
        return default_flt_val;

    return static_cast<float>(atof(result_str));
}

const char* CXml::ReadAttrib(XML_NODE start_node, const char* path, const size_t index, const char* attrib,
                             const char* default_str_val) const {
    const XML_NODE node = NavigateToNode(start_node, path, index);
    return ReadAttrib(node, attrib, default_str_val);
}

const char* CXml::ReadAttrib(const char* path, const size_t index, const char* attrib, const char* default_str_val) const {
    const XML_NODE node = NavigateToNode(path, index);
    return ReadAttrib(node, attrib, default_str_val);
}

const char* CXml::ReadAttrib(XML_NODE node, const char* attrib, const char* default_str_val) const {
    if (!node)
        return default_str_val;
    else {
        const char* result_str = node.elementAttribute(attrib);
        return result_str ? result_str : default_str_val;
    }
}

int CXml::ReadAttribInt(XML_NODE node, const char* attrib, const int default_int_val) const {
    const char* result_str = ReadAttrib(node, attrib, nullptr);

    if (result_str == nullptr)
        return default_int_val;

    return atoi(result_str);
}

int CXml::ReadAttribInt(const char* path, const size_t index, const char* attrib, const int default_int_val) const {
    const char* result_str = ReadAttrib(path, index, attrib, nullptr);

    if (result_str == nullptr)
        return default_int_val;

    return atoi(result_str);
}

int CXml::ReadAttribInt(XML_NODE start_node, const char* path, const size_t index, const char* attrib,
                        const int default_int_val) const {
    const char* result_str = ReadAttrib(start_node, path, index, attrib, nullptr);

    if (result_str == nullptr)
        return default_int_val;
    return atoi(result_str);
}

float CXml::ReadAttribFlt(const char* path, const size_t index, const char* attrib, const float default_flt_val) const {
    const char* result_str = ReadAttrib(path, index, attrib, nullptr);

    if (result_str == nullptr)
        return default_flt_val;

    return static_cast<float>(atof(result_str));
}

float CXml::ReadAttribFlt(XML_NODE start_node, const char* path, const size_t index, const char* attrib,
                          const float default_flt_val) const {
    const char* result_str = ReadAttrib(start_node, path, index, attrib, nullptr);

    if (result_str == nullptr)
        return default_flt_val;

    return static_cast<float>(atof(result_str));
}

float CXml::ReadAttribFlt(XML_NODE node, const char* attrib, const float default_flt_val) const {
    const char* result_str = ReadAttrib(node, attrib, nullptr);

    if (result_str == nullptr)
        return default_flt_val;

    return static_cast<float>(atof(result_str));
}

size_t CXml::GetNodesNum(const char* path, const size_t index, const char* tag_name) const {
    XML_NODE node;

    const XML_NODE root = GetLocalRoot() ? GetLocalRoot() : GetRoot();
    if (path != nullptr) {
        node = NavigateToNode(path, index);

        if (!node)
            node = root;
    } else
        node = root;

    if (!node)
        return 0;

    return GetNodesNum(node, tag_name);
}

size_t CXml::GetNodesNum(XML_NODE node, const char* tag_name) const {
    if (!node)
        return 0;

    XML_NODE el;

    if (!tag_name)
        el = node.firstChild();
    else
        el = node.firstChild(tag_name);

    size_t result = 0;

    while (el) {
        ++result;
        if (!tag_name)
            el = el.nextSibling();
        else
            el = el.nextSibling(tag_name);
    }

    return result;
}

// Find an element by its attribute

XML_NODE CXml::SearchForAttribute(const char* path, const size_t index, const char* tag_name, const char* attrib,
                                  const char* attrib_value_pattern) {
    const XML_NODE start_node = NavigateToNode(path, index);
    return SearchForAttribute(start_node, tag_name, attrib, attrib_value_pattern);
}

XML_NODE CXml::SearchForAttribute(XML_NODE start_node, const char* tag_name, const char* attrib,
                                  const char* attrib_value_pattern) const {
    while (start_node) {
        const char* attribStr = start_node.elementAttribute(attrib);
        const char* valueStr = start_node.elementValue();

        if (attribStr && 0 == xr_strcmp(attribStr, attrib_value_pattern) && valueStr &&
            0 == xr_strcmp(valueStr, tag_name)) {
            return start_node;
        }

        XML_NODE newEl = start_node.firstChild(tag_name);
        newEl = SearchForAttribute(newEl, tag_name, attrib, attrib_value_pattern);
        if (newEl)
            return newEl;

        start_node = start_node.nextSibling(tag_name);
    }
    return XML_NODE();
}

#ifdef DEBUG // debug & mixed

std::string_view CXml::CheckUniqueAttrib(XML_NODE start_node, const char* tag_name, const char* attrib_name) const {
    std::set<std::string_view> attribs;

    const auto tags_num = GetNodesNum(start_node, tag_name);

    for (int i = 0; i < tags_num; i++) {
        const std::string_view attrib = ReadAttrib(start_node, tag_name, i, attrib_name, nullptr);

        const auto iter = attribs.find(attrib);
        if (iter != attribs.end())
            return *iter;

        attribs.insert(attrib);
    }
    return std::string_view();
}
#endif

BOOL APIENTRY DllMain(HANDLE, DWORD, LPVOID) {
    return TRUE;
}
