#ifndef NOIR_TREE_WIND_H
#define NOIR_TREE_WIND_H

// Port of the supplied R2 v6 wind. Keep the formula identical across renderers.
// Inputs are WORLD position, tree base Y, vertex flexibility, engine wind, wave.w.
// wave.w already contains the engine time scale; do not multiply it again.
// Shared by the geometry and lighting/shadow passes of each renderer.
float3 noir_calc_tree_wind(float3 pos, float base, float frac, float4 wind, float time) {
    float H = max(pos.y - base, 0.0);
    float safe_H = min(H, 15.0); // Захист верхівок високих дерев

    float phase_offset = pos.x * 0.1 + pos.z * 0.1;
    float3 wind_vec = float3(wind.x, 0.0, wind.z);
    float base_wind_power = length(wind_vec);

    // Плавні пориви вітру
    float gust_time = time * 0.5 + phase_offset;
    float gust_noise = (sin(gust_time) + sin(gust_time * 1.42 + 1.0)) * 0.5;
    gust_noise = smoothstep(-0.5, 1.0, gust_noise);
    
    float gust_multiplier = 1.0 + gust_noise * 2.5; 
    float current_wind_power = base_wind_power * gust_multiplier;
    float3 current_wind_dir = wind_vec * gust_multiplier;
    float anim_wind_power = min(current_wind_power, 15.0);

    // 1. Нахил стовбура (РОЗУМНИЙ БЕТОН)
    // Множимо на frac. Тепер стовбур великого дерева біля землі (де frac=0) стоятиме мертво.
    // А от маленький кущ (де frac зростає швидко) буде гнутися цілком і природно.
    float bend_factor = safe_H * frac * 0.015; 
    float3 trunk_disp = current_wind_dir * bend_factor;

    // 2. Хитання великих гілок
    float sway_speed = time * 1.5 + phase_offset;
    float sway_amp = frac * safe_H * anim_wind_power * 0.02; 
    float3 branch_disp;
    branch_disp.x = sin(sway_speed) * current_wind_dir.x * sway_amp;
    branch_disp.y = 0.0;
    branch_disp.z = cos(sway_speed + 0.9) * current_wind_dir.z * sway_amp;

    // 3. Мікро-вібрація листя
    // frac * frac гарантує, що вібрують ТІЛЬКИ ті місця, де жорсткість максимальна (кінчики гілок/листя)
    float micro_speed = time * 15.0 + pos.x + pos.y;
    float micro_amp = (frac * frac) * anim_wind_power * 0.015;
    float3 leaf_disp;
    leaf_disp.x = sin(micro_speed) * micro_amp;
    leaf_disp.y = cos(micro_speed * 1.1) * micro_amp;
    leaf_disp.z = sin(micro_speed * 1.2) * micro_amp;

    float3 total_disp = trunk_disp + branch_disp + leaf_disp;
    
    // Компенсація висоти при нахилі
    total_disp.y -= length(trunk_disp.xz) * 0.1; 

    return total_disp;
}

#endif // NOIR_TREE_WIND_H
