#pragma once

#define CMD0(cls)                     \
    {                                 \
        static cls x##cls();          \
        Console->AddCommand(&x##cls); \
    }
#define CMD1(cls, p1)                 \
    {                                 \
        static cls x##cls(p1);        \
        Console->AddCommand(&x##cls); \
    }
#define CMD2(cls, p1, p2)             \
    {                                 \
        static cls x##cls(p1, p2);    \
        Console->AddCommand(&x##cls); \
    }
#define CMD3(cls, p1, p2, p3)          \
    {                                  \
        static cls x##cls(p1, p2, p3); \
        Console->AddCommand(&x##cls);  \
    }
#define CMD4(cls, p1, p2, p3, p4)          \
    {                                      \
        static cls x##cls(p1, p2, p3, p4); \
        Console->AddCommand(&x##cls);      \
    }

class ENGINE_API IConsole_Command {
public:
    friend class CConsole;
    typedef char TInfo[256];
    typedef char TStatus[256];
    typedef xr_vector<std::string> vecTips;
    typedef xr_vector<std::string> vecLRU;

protected:
    LPCSTR cName;
    bool bEnabled;
    bool bLowerCaseArgs;
    bool bEmptyArgsHandled;

    vecLRU m_LRU;

    static constexpr size_t LRU_MAX_COUNT = 10;

    bool EQ(LPCSTR S1, LPCSTR S2) { return xr_strcmp(S1, S2) == 0; }

public:
    IConsole_Command(LPCSTR N BENCH_SEC_SIGN)
        : cName(N), bEnabled(true), bLowerCaseArgs(true), bEmptyArgsHandled(false) {
        m_LRU.reserve(LRU_MAX_COUNT + 1);
        m_LRU.clear();
    }
    virtual ~IConsole_Command() {
        if (Console)
            Console->RemoveCommand(this);
    };

    BENCH_SEC_SCRAMBLEVTBL3

    LPCSTR Name() { return cName; }
    void InvalidSyntax() {
        TInfo I;
        Info(I);
        Msg("~ Invalid syntax in call to '%s'", cName);
        Msg("~ Valid arguments: %s", I);
    }
    virtual void Execute(LPCSTR args) = 0;
    virtual void Status(TStatus& S) { S[0] = 0; }
    virtual void Info(TInfo& I) { xr_strcpy(I, "(no arguments)"); }
    virtual void Save(IWriter* F) {
        TStatus S;
        Status(S);
        if (S[0])
            F->w_printf("%s %s\r\n", cName, S);
    }

    BENCH_SEC_SCRAMBLEVTBL2

    virtual void fill_tips(vecTips& tips, u32 mode) { add_LRU_to_tips(tips); }
    //			vecLRU&	LRU				() { return m_LRU; }
    virtual void add_to_LRU(std::string arg);
    void add_LRU_to_tips(vecTips& tips);

    // A command that keeps a shadow copy of its value refreshes it here. The SSFX menu controls
    // need this: what they edit is one float of a vector, while a check box speaks integers and
    // a drop-down speaks token names, so those commands store the real value elsewhere and keep
    // a copy of their own. CConsole::GetBool and GetInteger call this before reading the copy,
    // so the menu cannot show a stale state after something else wrote the vector.
    virtual void Sync() {}

}; // class IConsole_Command

class ENGINE_API CCC_Mask : public IConsole_Command {
protected:
    Flags32* value;
    u32 mask;

public:
    CCC_Mask(LPCSTR N, Flags32* V, u32 M) : IConsole_Command(N), value(V), mask(M){};
    const BOOL GetValue() const { return value->test(mask); }
    virtual void Execute(LPCSTR args) {
        if (EQ(args, "on"))
            value->set(mask, TRUE);
        else if (EQ(args, "off"))
            value->set(mask, FALSE);
        else if (EQ(args, "1"))
            value->set(mask, TRUE);
        else if (EQ(args, "0"))
            value->set(mask, FALSE);
        else
            InvalidSyntax();
    }
    virtual void Status(TStatus& S) { xr_strcpy(S, value->test(mask) ? "on" : "off"); }
    virtual void Info(TInfo& I) { xr_strcpy(I, "'on/off' or '1/0'"); }

    virtual void fill_tips(vecTips& tips, u32 mode) {
        TStatus str;
        xr_sprintf(str, sizeof(str), "%s  (current)  [on/off]", value->test(mask) ? "on" : "off");
        tips.push_back(str);
    }
};

class ENGINE_API CCC_ToggleMask : public IConsole_Command {
protected:
    Flags32* value;
    u32 mask;

public:
    CCC_ToggleMask(LPCSTR N, Flags32* V, u32 M) : IConsole_Command(N), value(V), mask(M) {
        bEmptyArgsHandled = TRUE;
    };
    const BOOL GetValue() const { return value->test(mask); }
    virtual void Execute(LPCSTR args) {
        value->set(mask, !GetValue());
        TStatus S;
        strconcat(sizeof(S), S, cName, " is ", value->test(mask) ? "on" : "off");
        Log(S);
    }
    virtual void Status(TStatus& S) { xr_strcpy(S, value->test(mask) ? "on" : "off"); }
    virtual void Info(TInfo& I) { xr_strcpy(I, "'on/off' or '1/0'"); }

    virtual void fill_tips(vecTips& tips, u32 mode) {
        TStatus str;
        xr_sprintf(str, sizeof(str), "%s  (current)  [on/off]", value->test(mask) ? "on" : "off");
        tips.push_back(str);
    }
};

class ENGINE_API CCC_Token : public IConsole_Command {
protected:
    u32* value;
    xr_token* tokens;

public:
    CCC_Token(LPCSTR N, u32* V, xr_token* T) : IConsole_Command(N), value(V), tokens(T){};

    virtual void Execute(LPCSTR args) {
        xr_token* tok = tokens;
        while (tok->name) {
            if (stricmp(tok->name, args) == 0) {
                *value = tok->id;
                break;
            }
            tok++;
        }
        if (!tok->name)
            InvalidSyntax();
    }
    virtual void Status(TStatus& S) {
        xr_token* tok = tokens;
        while (tok->name) {
            if (tok->id == (int)(*value)) {
                xr_strcpy(S, tok->name);
                return;
            }
            tok++;
        }
        xr_strcpy(S, "?");
        return;
    }
    virtual void Info(TInfo& I) {
        I[0] = 0;
        xr_token* tok = tokens;
        while (tok->name) {
            if (I[0])
                xr_strcat(I, "/");
            xr_strcat(I, tok->name);
            tok++;
        }
    }
    virtual xr_token* GetToken() { return tokens; }

    virtual void fill_tips(vecTips& tips, u32 mode) {
        TStatus str;
        bool res = false;
        xr_token* tok = GetToken();
        while (tok->name && !res) {
            if (tok->id == (int)(*value)) {
                xr_sprintf(str, sizeof(str), "%s  (current)", tok->name);
                tips.push_back(str);
                res = true;
            }
            tok++;
        }
        if (!res) {
            tips.push_back("---  (current)");
        }
        tok = GetToken();
        while (tok->name) {
            tips.push_back(tok->name);
            tok++;
        }
    }
};

class ENGINE_API CCC_Float : public IConsole_Command {
protected:
    float* value;
    float min, max;

public:
    CCC_Float(LPCSTR N, float* V, float _min = 0, float _max = 1)
        : IConsole_Command(N), value(V), min(_min), max(_max){};
    const float GetValue() const { return *value; };
    void GetBounds(float& fmin, float& fmax) const {
        fmin = min;
        fmax = max;
    }

    virtual void Execute(LPCSTR args) {
        float v = float(atof(args));
        if (v < (min - EPS) || v > (max + EPS))
            InvalidSyntax();
        else
            *value = v;
    }
    virtual void Status(TStatus& S) {
        xr_sprintf(S, sizeof(S), "%3.5f", *value);
        while (xr_strlen(S) && ('0' == S[xr_strlen(S) - 1]))
            S[xr_strlen(S) - 1] = 0;
    }
    virtual void Info(TInfo& I) {
        xr_sprintf(I, sizeof(I), "float value in range [%3.3f,%3.3f]", min, max);
    }
    virtual void fill_tips(vecTips& tips, u32 mode) {
        TStatus str;
        xr_sprintf(str, sizeof(str), "%3.5f  (current)  [%3.3f,%3.3f]", *value, min, max);
        tips.push_back(str);
        IConsole_Command::fill_tips(tips, mode);
    }
};

class ENGINE_API CCC_Vector3 : public IConsole_Command {
protected:
    Fvector* value;
    Fvector min, max;

public:
    CCC_Vector3(LPCSTR N, Fvector* V, const Fvector _min, const Fvector _max)
        : IConsole_Command(N), value(V) {
        min.set(_min);
        max.set(_max);
    };
    const Fvector GetValue() const { return *value; };
    Fvector* GetValuePtr() const { return value; };

    virtual void Execute(LPCSTR args) {
        Fvector v;
        // Accept both "x,y,z" and "(x,y,z)" — script-driven callers use the bracketed form.
        if (3 != sscanf(args, "%f,%f,%f", &v.x, &v.y, &v.z) &&
            3 != sscanf(args, "(%f,%f,%f)", &v.x, &v.y, &v.z)) {
            InvalidSyntax();
            return;
        }
        if (v.x < min.x || v.y < min.y || v.z < min.z) {
            InvalidSyntax();
            return;
        }
        if (v.x > max.x || v.y > max.y || v.z > max.z) {
            InvalidSyntax();
            return;
        }
        value->set(v);
    }
    virtual void Status(TStatus& S) {
        xr_sprintf(S, sizeof(S), "(%f, %f, %f)", value->x, value->y, value->z);
    }
    virtual void Info(TInfo& I) {
        xr_sprintf(I, sizeof(I), "vector3 in range [%e,%e,%e]-[%e,%e,%e]", min.x, min.y, min.z,
                   max.x, max.y, max.z);
    }
    virtual void fill_tips(vecTips& tips, u32 mode) {
        TStatus str;
        xr_sprintf(str, sizeof(str), "(%e, %e, %e)  (current)  [(%e,%e,%e)-(%e,%e,%e)]", value->x,
                   value->y, value->z, min.x, min.y, min.z, max.x, max.y, max.z);
        tips.push_back(str);
        IConsole_Command::fill_tips(tips, mode);
    }
};

class ENGINE_API CCC_Vector4 : public IConsole_Command {
protected:
    Fvector4* value;
    Fvector4 min, max;

public:
    CCC_Vector4(LPCSTR N, Fvector4* V, const Fvector4 _min, const Fvector4 _max)
        : IConsole_Command(N), value(V) {
        min.set(_min);
        max.set(_max);
    };
    const Fvector4 GetValue() const { return *value; };
    Fvector4* GetValuePtr() const { return value; };

    virtual void Execute(LPCSTR args) {
        Fvector4 v;
        if (4 != sscanf(args, "%f,%f,%f,%f", &v.x, &v.y, &v.z, &v.w) &&
            4 != sscanf(args, "(%f,%f,%f,%f)", &v.x, &v.y, &v.z, &v.w)) {
            InvalidSyntax();
            return;
        }
        if (v.x < min.x || v.y < min.y || v.z < min.z || v.w < min.w) {
            InvalidSyntax();
            return;
        }
        if (v.x > max.x || v.y > max.y || v.z > max.z || v.w > max.w) {
            InvalidSyntax();
            return;
        }
        value->set(v);
    }
    virtual void Status(TStatus& S) {
        xr_sprintf(S, sizeof(S), "(%f, %f, %f, %f)", value->x, value->y, value->z, value->w);
    }
    virtual void Info(TInfo& I) {
        xr_sprintf(I, sizeof(I), "vector4 in range [%e,%e,%e,%e]-[%e,%e,%e,%e]", min.x, min.y,
                   min.z, min.w, max.x, max.y, max.z, max.w);
    }
    virtual void fill_tips(vecTips& tips, u32 mode) {
        TStatus str;
        xr_sprintf(str, sizeof(str), "(%e, %e, %e, %e)  (current)", value->x, value->y, value->z,
                   value->w);
        tips.push_back(str);
        IConsole_Command::fill_tips(tips, mode);
    }
};

class ENGINE_API CCC_Integer : public IConsole_Command {
protected:
    int* value;
    int min, max;

public:
    const int GetValue() const { return *value; };
    void GetBounds(int& imin, int& imax) const {
        imin = min;
        imax = max;
    }

    CCC_Integer(LPCSTR N, int* V, int _min = 0, int _max = 999)
        : IConsole_Command(N), value(V), min(_min), max(_max){};

    virtual void Execute(LPCSTR args) {
        int v = atoi(args);
        if (v < min || v > max)
            InvalidSyntax();
        else
            *value = v;
    }
    virtual void Status(TStatus& S) { itoa(*value, S, 10); }
    virtual void Info(TInfo& I) {
        xr_sprintf(I, sizeof(I), "integer value in range [%d,%d]", min, max);
    }
    virtual void fill_tips(vecTips& tips, u32 mode) {
        TStatus str;
        xr_sprintf(str, sizeof(str), "%d  (current)  [%d,%d]", *value, min, max);
        tips.push_back(str);
        IConsole_Command::fill_tips(tips, mode);
    }
};

// Screen Space Shaders: the three command classes the native SSFX settings page is built on.
//
// Almost every SSFX setting lives in one float of a vector - ssfx_water_setup1 carries four of
// them at once - because that is how the author's shaders read them. His own menu is Lua: it
// stores the setting, runs it through a little arithmetic and sends the whole vector. Our menu
// has no Lua layer, so each control writes its own component through a command of its own, and
// the two widgets that do not speak "one float of a vector" need these classes:
//
//   * a check box reads its state with CConsole::GetBool, which answers only for CCC_Mask and
//     CCC_Integer. Pointed at a CCC_Float it would read back unchecked no matter what was saved,
//     so CCC_ssfx_ui_bool is an integer command that stores into the float.
//   * a drop-down reads and writes token names through CConsole::GetXRToken, which answers only
//     for CCC_Token. CCC_ssfx_ui_list is a token command whose chosen id lands in the float.
//     The author offers these settings as a list rather than a slider because several of them
//     are compiled into the shader, and only his own preset values have a compiled variant.
//   * CCC_ssfx_ui_list_i is the same for a setting whose engine variable is an int.
//
// Each keeps a shadow copy for its base class to work on and refreshes it in Sync(), so a value
// written from anywhere else - user.ltx, the console, one of the author's scripts - still shows
// up in the menu.
//
// A list can only show a value that is on it. The vector commands accept anything inside their
// range ( ssfx_pom takes 20 samples as readily as 16 or 24 ), so the two list classes read the
// variable as the nearest entry: the drop-down always names a real entry, and the first write
// through the list puts the variable on it.
class ENGINE_API CCC_ssfx_ui_bool : public CCC_Integer {
protected:
    float* m_target;
    int m_shadow;

public:
    CCC_ssfx_ui_bool(LPCSTR N, float* V) : CCC_Integer(N, &m_shadow, 0, 1), m_target(V) {
        m_shadow = (*V > 0.5f) ? 1 : 0;
    }
    virtual void Sync() { m_shadow = (*m_target > 0.5f) ? 1 : 0; }
    virtual void Execute(LPCSTR args) {
        Sync();
        CCC_Integer::Execute(args);
        *m_target = float(m_shadow);
    }
    virtual void Status(TStatus& S) {
        Sync();
        CCC_Integer::Status(S);
    }
    virtual void fill_tips(vecTips& tips, u32 mode) {
        Sync();
        CCC_Integer::fill_tips(tips, mode);
    }
};

class ENGINE_API CCC_ssfx_ui_list : public CCC_Token {
protected:
    float* m_target;
    u32 m_shadow;

public:
    // The id of the entry closest to v. The first entry wins a tie.
    static u32 nearest(xr_token* tok, float v) {
        int best = tok->id;
        float best_d = flt_max;
        for (; tok->name; tok++) {
            float d = float(tok->id) - v;
            if (d < 0.0f)
                d = -d;
            if (d < best_d) {
                best = tok->id;
                best_d = d;
            }
        }
        return u32(best);
    }

    CCC_ssfx_ui_list(LPCSTR N, float* V, xr_token* T) : CCC_Token(N, &m_shadow, T), m_target(V) {
        m_shadow = nearest(T, *V);
    }
    virtual void Sync() { m_shadow = nearest(tokens, *m_target); }
    virtual void Execute(LPCSTR args) {
        Sync();
        CCC_Token::Execute(args);
        *m_target = float(int(m_shadow));
    }
    virtual void Status(TStatus& S) {
        Sync();
        CCC_Token::Status(S);
    }
    virtual void fill_tips(vecTips& tips, u32 mode) {
        Sync();
        CCC_Token::fill_tips(tips, mode);
    }
};

class ENGINE_API CCC_ssfx_ui_list_i : public CCC_Token {
protected:
    int* m_target;
    u32 m_shadow;

public:
    CCC_ssfx_ui_list_i(LPCSTR N, int* V, xr_token* T) : CCC_Token(N, &m_shadow, T), m_target(V) {
        m_shadow = CCC_ssfx_ui_list::nearest(T, float(*V));
    }
    virtual void Sync() { m_shadow = CCC_ssfx_ui_list::nearest(tokens, float(*m_target)); }
    virtual void Execute(LPCSTR args) {
        Sync();
        CCC_Token::Execute(args);
        *m_target = int(m_shadow);
    }
    virtual void Status(TStatus& S) {
        Sync();
        CCC_Token::Status(S);
    }
    virtual void fill_tips(vecTips& tips, u32 mode) {
        Sync();
        CCC_Token::fill_tips(tips, mode);
    }
};

class ENGINE_API CCC_String : public IConsole_Command {
protected:
    LPSTR value;
    int size;

public:
    CCC_String(LPCSTR N, LPSTR V, int _size = 2) : IConsole_Command(N), value(V), size(_size) {
        bLowerCaseArgs = FALSE;
        R_ASSERT(V);
        R_ASSERT(size > 1);
    };

    virtual void Execute(LPCSTR args) { strncpy_s(value, size, args, size - 1); }
    virtual void Status(TStatus& S) { xr_strcpy(S, value); }
    virtual void Info(TInfo& I) {
        xr_sprintf(I, sizeof(I), "string with up to %d characters", size);
    }
    virtual void fill_tips(vecTips& tips, u32 mode) {
        tips.push_back((LPCSTR)value);
        IConsole_Command::fill_tips(tips, mode);
    }
};

class ENGINE_API CCC_LoadCFG : public IConsole_Command {
public:
    virtual bool allow(LPCSTR cmd) { return true; };
    CCC_LoadCFG(LPCSTR N);
    virtual void Execute(LPCSTR args);
};

class ENGINE_API CCC_LoadCFG_custom : public CCC_LoadCFG {
    string64 m_cmd;

public:
    CCC_LoadCFG_custom(LPCSTR cmd);
    virtual bool allow(LPCSTR cmd);
};