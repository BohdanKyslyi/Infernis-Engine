-- DX9: show the second world view through the optic without R3/R4 glass effects.
function normal(shader, t_base, t_second, t_detail)
    shader:begin("model_scope_lense", "model_scope_lense")
        :fog(false)
        :zb(false, false)
        :blend(true, blend.srcalpha, blend.invsrcalpha)
        :aref(false, 0)
        :sorting(2, true)
    shader:sampler("s_base"):texture(t_base):clamp():f_linear()
    shader:sampler("s_vp2"):texture("$user$scope_lens"):clamp():f_linear()
end
