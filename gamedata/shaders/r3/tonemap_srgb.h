//=================================================================================================
//Gamma Correct Tonemapping without Color Grading
//=================================================================================================

float3 tonemap_sRGB(float3 x, float w )
{	
	//convert into linear gamma space
	x = SRGBToLinear(x);

	//reinhard tonemapping
	x = x/(x+1);
	x /= w/(w+1);

	//float fWhiteIntensitySQR = w*w;
	//x = x/(1+(x/fWhiteIntensitySQR));

	//convert into sRGB gamma space
	x = LinearTosRGB(x);
	
	//return with saturate, everything should be in LDR sRGB
	return saturate(x);
}

