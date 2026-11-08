// Not a donor file. The engine asks for this name when a branch texture has a detail
// texture ( uber_deffer.cpp adds _d ), and the reference set has no file for it, so the
// branch would fall back to a stub. It is the reference's own branch shader with the
// detail layer switched on, built the way the stock _d variants are built.
#define USE_TDETAIL
#include "deffer_tree_branch_flat.vs"
