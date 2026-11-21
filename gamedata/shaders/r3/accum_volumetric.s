-- Screen Space Shaders on / off ( ssfx_enable ): with SSS on ssfx_enabled is true and the
-- SSS version is loaded; with SSS off, and on DX10, the game's own version after else.
-- Both are copied unchanged ( _ssfx_work\transfer_shaders.py ).
if ssfx_enabled then
function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("accum_volumetric", "accum_volumetric")
			: fog		(false)
			: zb 		(true,false)
			: blend		(true,blend.one,blend.one)
			: sorting	(2, false)

	shader:dx10texture	("s_lmap", t_base)	
	shader:dx10texture	("s_smap", "null")
	shader:dx10texture	("s_position",	"$user$position")

	shader:dx10sampler	("smp_rtlinear")
	shader:dx10sampler	("smp_smap")
	shader:dx10sampler	("smp_jitter")
end
else
function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("accum_volumetric", "accum_volumetric")
			: fog		(false)
			: zb 		(true,false)
			: blend		(true,blend.one,blend.one)
--			: aref 		(true,0)
			: sorting	(2, false)
--	TODO: Implement sampler states
--	shader:sampler	("s_lmap")      :texture	(t_base): clamp()
--	shader:sampler	("s_smap")      :texture	("null")
--	shader:sampler	("s_noise")    	:texture("fx\\fx_noise")	: f_linear ()

	shader:dx10texture	("s_lmap", t_base)	
	shader:dx10texture	("s_smap", "null")
	shader:dx10texture	("s_noise", "fx\\fx_noise")

	shader:dx10sampler	("smp_rtlinear")
	shader:dx10sampler	("smp_linear")
--	shader:dx10sampler	("smp_jitter")
	shader:dx10sampler	("smp_smap")
end
end
