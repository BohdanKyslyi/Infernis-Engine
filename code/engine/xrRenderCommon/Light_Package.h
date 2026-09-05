#pragma once

#include "light.h"

class light_Package {
public:
    xr_vector<light*> v_point;
    xr_vector<light*> v_spot;
    xr_vector<light*> v_shadowed;

public:
    light_Package() = default;
    ~light_Package() = default;

    void clear() noexcept;
    void sort();
};