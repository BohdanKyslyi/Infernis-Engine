#include "stdafx.h"
#include "DetailManager.h"
#include <algorithm> // Для std::copy, std::move

void CDetailManager::cache_Initialize() {
    // Centroid
    cache_cx = 0;
    cache_cz = 0;

    // Initialize cache-grid
    Slot* slt = cache_pool;
    for (u32 i = 0; i < dm_cache_line; ++i) {
        for (u32 j = 0; j < dm_cache_line; ++j, ++slt) {
            cache[i][j] = slt;
            cache_Task(j, i, slt);
        }
    }
    VERIFY(cache_Validate());

    for (int _mz1 = 0; _mz1 < dm_cache1_line; ++_mz1) {
        for (int _mx1 = 0; _mx1 < dm_cache1_line; ++_mx1) {
            CacheSlot1& MS = cache_level1[_mz1][_mx1];
            for (int _z = 0; _z < dm_cache1_count; ++_z) {
                for (int _x = 0; _x < dm_cache1_count; ++_x) {
                    MS.slots[_z * dm_cache1_count + _x] =
                        &cache[_mz1 * dm_cache1_count + _z][_mx1 * dm_cache1_count + _x];
                }
            }
        }
    }
}

CDetailManager::Slot* CDetailManager::cache_Query(int r_x, int r_z) {
    const int gx = w2cg_X(r_x + cache_cx);
    VERIFY(gx >= 0 && gx < dm_cache_line);
    
    const int gz = w2cg_Z(r_z + cache_cz);
    VERIFY(gz >= 0 && gz < dm_cache_line);
    
    return cache[gz][gx];
}

void CDetailManager::cache_Task(int gx, int gz, Slot* D) {
    const int sx = cg2w_X(gx);
    const int sz = cg2w_Z(gz);
    DetailSlot& DS = QueryDB(sx, sz);

    D->empty = (DS.id0 == DetailSlot::ID_Empty) && (DS.id1 == DetailSlot::ID_Empty) &&
               (DS.id2 == DetailSlot::ID_Empty) && (DS.id3 == DetailSlot::ID_Empty);

    // Unpacking
    const u32 old_type = D->type;
    D->type = stPending;
    D->sx = sx;
    D->sz = sz;

    D->vis.box.min.set(sx * dm_slot_size, DS.r_ybase(), sz * dm_slot_size);
    D->vis.box.max.set(D->vis.box.min.x + dm_slot_size, DS.r_ybase() + DS.r_yheight(), D->vis.box.min.z + dm_slot_size);
    D->vis.box.grow(EPS_L);

    for (u32 i = 0; i < dm_obj_in_slot; ++i) {
        D->G[i].id = DS.r_id(i);
        
        // МАКСИМАЛЬНА ОПТИМІЗАЦІЯ: Range-based цикл для очищення пам'яті
        for (auto* item : D->G[i].items) {
            poolSI.destroy(item);
        }
        D->G[i].items.clear();
    }

    if (old_type != stPending) {
        VERIFY(stPending == D->type);
        cache_task.push_back(D);
    }
}

BOOL CDetailManager::cache_Validate() {
    for (int z = 0; z < dm_cache_line; ++z) {
        for (int x = 0; x < dm_cache_line; ++x) {
            const int w_x = cg2w_X(x);
            const int w_z = cg2w_Z(z);
            const Slot* D = cache[z][x];

            if (D->sx != w_x || D->sz != w_z)
                return FALSE;
        }
    }
    return TRUE;
}

void CDetailManager::cache_Update(int v_x, int v_z, Fvector& view, int limit) {
    const bool bNeedMegaUpdate = (cache_cx != v_x) || (cache_cz != v_z);
    
    // ***** Cache shift (Data-Oriented Optimizations)
    while (cache_cx != v_x) {
        if (v_x > cache_cx) {
            // Shift matrix to left (x)
            cache_cx++;
            for (int z = 0; z < dm_cache_line; ++z) {
                Slot* S = cache[z][0];
                std::move(&cache[z][1], &cache[z][dm_cache_line], &cache[z][0]);
                cache[z][dm_cache_line - 1] = S;
                cache_Task(dm_cache_line - 1, z, S);
            }
        } else {
            // Shift matrix to right (x)
            cache_cx--;
            for (int z = 0; z < dm_cache_line; ++z) {
                Slot* S = cache[z][dm_cache_line - 1];
                std::move_backward(&cache[z][0], &cache[z][dm_cache_line - 1], &cache[z][dm_cache_line]);
                cache[z][0] = S;
                cache_Task(0, z, S);
            }
        }
    }
    
    while (cache_cz != v_z) {
        if (v_z > cache_cz) {
            // Shift matrix down (z) - МАКСИМАЛЬНА ОПТИМІЗАЦІЯ: Зсув цілими рядками
            cache_cz++;
            Slot* temp_row[dm_cache_line];
            std::copy(std::begin(cache[dm_cache_line - 1]), std::end(cache[dm_cache_line - 1]), std::begin(temp_row));
            
            for (int z = dm_cache_line - 1; z > 0; --z) {
                std::copy(std::begin(cache[z - 1]), std::end(cache[z - 1]), std::begin(cache[z]));
            }
            
            std::copy(std::begin(temp_row), std::end(temp_row), std::begin(cache[0]));
            
            for (int x = 0; x < dm_cache_line; ++x) {
                cache_Task(x, 0, cache[0][x]);
            }
        } else {
            // Shift matrix up (z) - МАКСИМАЛЬНА ОПТИМІЗАЦІЯ: Зсув цілими рядками
            cache_cz--;
            Slot* temp_row[dm_cache_line];
            std::copy(std::begin(cache[0]), std::end(cache[0]), std::begin(temp_row));
            
            for (int z = 1; z < dm_cache_line; ++z) {
                std::copy(std::begin(cache[z]), std::end(cache[z]), std::begin(cache[z - 1]));
            }
            
            std::copy(std::begin(temp_row), std::end(temp_row), std::begin(cache[dm_cache_line - 1]));
            
            for (int x = 0; x < dm_cache_line; ++x) {
                cache_Task(x, dm_cache_line - 1, cache[dm_cache_line - 1][x]);
            }
        }
    }

    // Task performer
    const bool bFullUnpack = (cache_task.size() == dm_cache_size);
    if (bFullUnpack) limit = dm_cache_size;

    for (int iteration = 0; !cache_task.empty() && (iteration < limit); ++iteration) {
        u32 best_id = 0;

        if (bFullUnpack) {
            best_id = cache_task.size() - 1;
        } else {
            float best_dist = flt_max;
            for (u32 entry = 0; entry < cache_task.size(); ++entry) {
                Fvector C;
                cache_task[entry]->vis.box.getcenter(C);
                const float D = view.distance_to_sqr(C);

                if (D < best_dist) {
                    best_dist = D;
                    best_id = entry;
                }
            }
        }

        // МАКСИМАЛЬНА ОПТИМІЗАЦІЯ: O(1) видалення елемента з svector (Swap and Pop)
        cache_Decompress(cache_task[best_id]);
        cache_task[best_id] = cache_task.back();
        cache_task.pop_back();
    }

    if (bNeedMegaUpdate) {
        for (int _mz1 = 0; _mz1 < dm_cache1_line; ++_mz1) {
            for (int _mx1 = 0; _mx1 < dm_cache1_line; ++_mx1) {
                CacheSlot1& MS = cache_level1[_mz1][_mx1];
                MS.empty = TRUE;
                MS.vis.clear();
                
                for (int _i = 0; _i < dm_cache1_count * dm_cache1_count; ++_i) {
                    Slot& S = *(*MS.slots[_i]);
                    MS.vis.box.merge(S.vis.box);
                    if (!S.empty) MS.empty = FALSE;
                }
                MS.vis.box.getsphere(MS.vis.sphere.P, MS.vis.sphere.R);
            }
        }
    }
}

DetailSlot& CDetailManager::QueryDB(int sx, int sz) {
    const int db_x = sx + dtH.offs_x;
    const int db_z = sz + dtH.offs_z;
    
    if (db_x >= 0 && db_x < static_cast<int>(dtH.size_x) && 
        db_z >= 0 && db_z < static_cast<int>(dtH.size_z)) {
        const u32 linear_id = db_z * dtH.size_x + db_x;
        return dtSlots[linear_id];
    }
    
    // Empty slot
    DS_empty.w_id(0, DetailSlot::ID_Empty);
    DS_empty.w_id(1, DetailSlot::ID_Empty);
    DS_empty.w_id(2, DetailSlot::ID_Empty);
    DS_empty.w_id(3, DetailSlot::ID_Empty);
    return DS_empty;
}