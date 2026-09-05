#include "stdafx.h"
#include "Light_Package.h"
#include <algorithm> // Для std::stable_sort

void light_Package::clear() noexcept {
    v_point.clear();
    v_spot.clear();
    v_shadowed.clear();
}

#if (RENDER == R_R2) || (RENDER == R_R3) || (RENDER == R_R4)

// C++17: Безпечний компаратор з const вказівниками та noexcept
inline bool pred_light_cmp(const light* _1, const light* _2) noexcept {
    if (_1->vis.pending) {
        if (_2->vis.pending)
            return _1->vis.query_order > _2->vis.query_order; // q-order
        return false; // _2 should be first
    } 
    
    if (_2->vis.pending)
        return true; // _1 should be first
        
    return _1->range > _2->range; // sort by range
}

void light_Package::sort() {
    // resort lights (pending -> at the end), maintain stable order
    std::stable_sort(std::begin(v_point), std::end(v_point), pred_light_cmp);
    std::stable_sort(std::begin(v_spot), std::end(v_spot), pred_light_cmp);
    std::stable_sort(std::begin(v_shadowed), std::end(v_shadowed), pred_light_cmp);
}

#endif // (RENDER==R_R2) || (RENDER==R_R3) || (RENDER==R_R4)