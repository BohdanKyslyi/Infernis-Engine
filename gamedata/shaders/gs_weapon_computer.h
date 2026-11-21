uniform float4 weapon_computer_display; // charge, interference, condition, ammo

float weapon_computer_noise(float2 uv)
{
    float2 cell = floor(uv * 128.0);
    float hash = frac(dot(cell, float2(0.06711, 0.00584)) + frac(timers.z * 7.0));
    float noise = frac(hash * 52.9829);
    return noise * weapon_computer_display.y;
}
