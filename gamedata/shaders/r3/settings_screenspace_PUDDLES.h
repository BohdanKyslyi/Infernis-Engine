// Weather_ATMOS copy ( weather step 5, item 6 ):
// gamedata\shaders\r3\settings_screenspace_PUDDLES.h with the weather package's numbers (
// xray_monolith\Weather_anomaly\shaders\r3\settings_screenspace_PUDDLES.h ) -
// G_PUDDLES_GLOBAL_SIZE 1.0f -> 1.1f, G_PUDDLES_SIZE 0.8f -> 1.0f, G_PUDDLES_BORDER_HARDNESS 0.7f
// -> 0.65f, G_PUDDLES_TERRAIN_EXTRA_WETNESS 0.15f -> 0.25f, G_PUDDLES_REFLECTIVITY 0.4f -> 0.5f;
// G_PUDDLES_ALLWAYS stays on ( the user, 2026-10-28 ), the package has it off. The addon is
// mounted only while SSS runs on DX11. Made by _ssfx_work\weather_addon_shader_build.py: build it
// again when gamedata's file changes.
// [ SETTINGS ] [ PUDDLES ]

// NOTE: Reflection quality is defined by G_SSR_QUALITY ( settings_screenspace_SSR.h )

#define G_PUDDLES_GLOBAL_SIZE				1.1f	// Puddles global size. ( This affect distance and puddles size )
#define G_PUDDLES_SIZE						1.0f	// Puddles individual size. ( This only affect puddles size )
#define G_PUDDLES_BORDER_HARDNESS			0.65f	// Border hardness. ( 1.0f = Extremely defined border )
#define G_PUDDLES_TERRAIN_EXTRA_WETNESS		0.25f	// Terrain extra wetness when raining. ( Over time like puddles )
#define G_PUDDLES_REFLECTIVITY				0.5f	// Reflectivity. ( 1.0f = Mirror like )
#define G_PUDDLES_TINT						float3(0.66f, 0.63f, 0.6f) // Puddles tint RGB.

#define G_PUDDLES_RIPPLES							// Comment to disable ripples
#define G_PUDDLES_RIPPLES_SCALE				1.0f	// Ripples scale
#define G_PUDDLES_RIPPLES_INTENSITY			1.0f	// Puddles ripples intensity
#define G_PUDDLES_RIPPLES_RAINING_INT		0.1f	// Extra ripple intensity when raining ( Affected by rain intensity )
#define G_PUDDLES_RIPPLES_SPEED				1.0f	// Puddles ripples movement speed

#define G_PUDDLES_RAIN_RIPPLES_INTENSITY	1.0f	// Rain ripples intensity
#define G_PUDDLES_RAIN_RIPPLES_SCALE		1.0f	// Rain ripples scale

#define G_PUDDLES_REFRACTION_INTENSITY		1.0f	// Refraction intensity

#define G_PUDDLES_ALLWAYS					// Uncomment to allways render puddles ( Raining or not )