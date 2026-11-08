-- Screen Space Shaders on / off ( ssfx_enable ): with SSS on ssfx_enabled is true and the
-- SSS version is loaded; with SSS off, and on DX10, the game's own version after else.
-- Both are copied unchanged ( _ssfx_work\transfer_shaders.py ).
if ssfx_enabled then
function normal		(shader, t_base, t_second, t_detail)
	shader	: begin		("deffer_model_flat", "deffer_base_flat")
			: fog		(false)
			: emissive 	(true)

	shader:dx10texture("s_base", t_base)
	shader:dx10sampler("smp_base")
	shader:dx10stencil(true, cmp_func.always, 255 , 127, stencil_op.keep, stencil_op.replace, stencil_op.keep)
	shader:dx10stencil_ref(1)
end

function l_special	(shader, t_base, t_second, t_detail)
	-- Screen Space Shaders: the pseudo-PBR module's pairing, from the Enhanced Shaders copy of this
	-- file. Its accum_emissive.ps reads the texture through p_flat, so the pass needs a vertex shader
	-- that hands texture coordinates over, and the texture itself; models_selflight.vs writes the
	-- position only. deffer_model_flat computes the position the same way ( m_WVP, then
	-- ssfx_taa_jitter ) and is what the normal pass above draws with. models_selflightl.s already
	-- pairs it with the stock accum_emissivel.ps, which reads nothing, so the module-off case is the
	-- same as there.
	shader	: begin		("deffer_model_flat", "accum_emissive")
			: zb 		(true,false)
			: fog		(false)
			: emissive 	(true)

	shader:dx10texture	("s_base", t_base)
	shader:dx10sampler	("smp_base")
end
else
function normal		(shader, t_base, t_second, t_detail)
	shader:begin	("deffer_model_flat","deffer_base_flat")
			: fog		(false)
			: emissive 	(true)
	--shader:sampler	("s_base")      :texture	(t_base)
	shader:dx10texture	("s_base",	t_base)
	shader:dx10sampler	("smp_base")
	shader:dx10stencil	( 	true, cmp_func.always, 
							255 , 127, 
							stencil_op.keep, stencil_op.replace, stencil_op.keep)
	shader:dx10stencil_ref	(1)
end

function l_special	(shader, t_base, t_second, t_detail)
	shader:begin	("shadow_direct_model",	"accum_emissive")
			: zb 		(true,false)
			: fog		(false)
			: emissive 	(true)
end
end
