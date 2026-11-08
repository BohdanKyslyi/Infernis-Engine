#include "stdafx.h"
#include "dxWallMarkArray.h"

#include "dxUIShader.h"

void dxWallMarkArray::Copy(IWallMarkArray& _in) { *this = *(dxWallMarkArray*)&_in; }

dxWallMarkArray::~dxWallMarkArray() {
    for (auto it = m_CollideMarks.begin(); it != m_CollideMarks.end(); ++it)
        it->destroy();
}

void dxWallMarkArray::AppendMark(LPCSTR s_textures) {
    ref_shader s;
#if RENDER == R_R4
    // Screen Space Shaders, Monolith dxWallMarkArray.cpp: blood marks (every texture named
    // wm_blood_*) are drawn with the SSFX blood shader when the blood module is installed.
    LPCSTR sh_name = "effects\\wallmark";
    if (RImplementation.o.ssfx_blood && strstr(s_textures, "wm_blood_"))
        sh_name = "effects\\wallmark_blood";
    s.create(sh_name, s_textures);
#else
    s.create("effects\\wallmark", s_textures);
#endif
    m_CollideMarks.push_back(s);
}

void dxWallMarkArray::clear() { return m_CollideMarks.clear(); }

bool dxWallMarkArray::empty() { return m_CollideMarks.empty(); }

wm_shader dxWallMarkArray::GenerateWallmark() {
    wm_shader res;
    if (!m_CollideMarks.empty())
        ((dxUIShader*)&*res)->hShader = m_CollideMarks[::Random.randI(0, m_CollideMarks.size())];
    return res;
}

ref_shader* dxWallMarkArray::dxGenerateWallmark() {
    return m_CollideMarks.empty() ? NULL
                                  : &m_CollideMarks[::Random.randI(0, m_CollideMarks.size())];
}