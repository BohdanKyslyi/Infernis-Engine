-- Assign models\lense_scope to the physical ocular lens in the HUD OGF.
-- The base texture holds the reticle: its alpha separates reticle from the scene.
function normal(shader, t_base, t_second, t_detail)
    shader:begin("model_scope_lense", "model_scope_lense")
        :fog(false)
        -- Keep the housing in front of the physical lens at rest. While
        -- aiming, the pixel shader brings only the optical aperture forward.
        :zb(true, false)
        :blend(true, blend.srcalpha, blend.invsrcalpha)
        :aref(false, 0)
        :sorting(2, true)
        :distort(false)
    shader:dx10texture("s_base", t_base)
    shader:dx10texture("s_vp2", "$user$scope_lens")
    shader:dx10sampler("smp_base")
    shader:dx10sampler("smp_rtlinear")
end
