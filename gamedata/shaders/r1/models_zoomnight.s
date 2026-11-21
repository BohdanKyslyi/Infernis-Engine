function normal(shader, t_base, t_second, t_detail)
    shader:begin("model_def_lplanes", "base_lplanes")
        :fog(false)
        :zb(true, false)
        :color_write_enable(false, false, false, false)
    shader:sampler("s_base"):texture(t_base)
end
