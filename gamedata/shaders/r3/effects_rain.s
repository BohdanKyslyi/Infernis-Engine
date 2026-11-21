-- Screen Space Shaders on / off ( ssfx_enable ): with SSS on ssfx_enabled is true and the
-- SSS version is loaded; with SSS off, and on DX10, the game's own version after else.
-- Both are copied unchanged ( _ssfx_work\transfer_shaders.py ).
if ssfx_enabled then
-- @ Version: SCREEN SPACE SHADERS - UPDATE 17
-- @ Description: Rain - Shader Config
-- @ Author: https://www.moddb.com/members/ascii1457
-- @ Mod: https://www.moddb.com/mods/stalker-anomaly/addons/screen-space-shaders

function normal(shader, t_base, t_second, t_detail)
	shader	: begin	("effects_rain","effects_rain")
			: zb	(true,false)
			: blend	(true,blend.srcalpha,blend.invsrcalpha)
			: aref 		(true,0)

	shader : dx10texture("s_base", "fx\\rain_drop")
	shader : dx10texture("s_position", "$user$position")
	shader : dx10texture("s_accumulator", "$user$accum")
	shader : dx10texture("ssfx_color_buffer", "$user$ssfx_rain")

	shader 	: dx10sampler	("smp_rtlinear")
	shader 	: dx10sampler	("smp_nofilter")
end
else
function normal		(shader, t_base, t_second, t_detail)
	shader	: begin	("stub_default","stub_default")
			: zb	(true,false)
			: blend	(true,blend.srcalpha,blend.invsrcalpha)
--	TODO: DX10: implement aref for this shader
			: aref 		(true,0)

	shader 	: dx10texture	("s_base", t_base)
--	TODO: DX10: Clamp
	shader 	: dx10sampler	("smp_base") --	Clamp
end
end
