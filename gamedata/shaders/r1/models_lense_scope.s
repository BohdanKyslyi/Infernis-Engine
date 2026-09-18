-- R1 keeps the original 2D scope overlay. The HUD optic only needs a
-- textured glass placeholder; the second world view is exclusive to R2+.
-- The filename maps the OGF material models\lense_scope to a Lua shader.
function normal(shader, t_base, t_second, t_detail)
    shader:begin("model_env_lq", "model_env_lq")
        :fog(false)
        :zb(true, false)
        :blend(true, blend.srcalpha, blend.invsrcalpha)
        :sorting(2, true)
    shader:sampler("s_base"):texture(t_base):clamp():f_linear()
    shader:sampler("s_env"):texture(t_base):clamp():f_linear()
end
