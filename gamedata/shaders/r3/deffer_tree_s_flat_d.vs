// Screen Space Shaders on / off ( ssfx_enable ): with SSS on the DX11 renderer defines
// IE_SSFX_ENABLED and the SSS version is compiled; with SSS off, and on DX10, the game's
// own version after #else. Item 7b ( 2026-11-11 ): the SSS version is Monolith's own file ( the
// SSS addon has none ); the game's 1.6.00 file sends def_objects_lod objects ( vehicles ) into the
// SSS tree shader, whose sky light never drops below 30% - a glow underground. Both versions are
// copied unchanged ( _ssfx_work\tree_s_wrappers_7b.py ).
#ifdef IE_SSFX_ENABLED
#define	USE_TDETAIL
#include "deffer_tree_s_flat.vs"
#else
#define USE_TREEWAVE
#include "deffer_tree_flat_d.vs"
#endif
