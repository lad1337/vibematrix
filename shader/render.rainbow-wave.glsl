#extension GL_OES_standard_derivatives : enable

precision highp float;


#define power 1.
#define zoomOut 3.
#define rot 1.
#define iter 10.
#define huePower 0.7
#define glow 0.5
#define distortScale 0.8
#define distortPower 0.45
#define Speed 1.5
#define Brightness 0.3

vec4 render( vec2 p )
{
    // Normalized pixel coordinates (from 0 to 1)
    vec2 uv = p;
    float WaveSpeed = load * 2.5;

	vec2 XYScale = vec2(1.,1.);
	vec2 XYMove = vec2(0.0,0.0);

    uv *= zoomOut;
	uv.xy = uv.xy * XYScale;
	uv.xy = uv.xy + XYMove;
	vec3 finalCol = vec3(0,0,0);
	float halfDistort = distortScale / 0.5;
	float distortsc2 = distortScale / distortScale + halfDistort;
    
	for(float i = 1.0; i < iter; i++){
		uv.x += distortPower / i * sin(i * distortScale * uv.y - time * Speed);
		uv.y += distortPower / i * sin(i * distortsc2 * uv.x + time * Speed);
	}
	vec3 col = vec3(vec3(glow,glow,glow)/sin(time*WaveSpeed-length(uv.yx) - uv.y));
	finalCol = vec3(col);//*col);
    vec3 Color = vec3(1.,1.,1.) * Brightness;
	Color = Color*Color * 0.5 + 0.5*cos(time+uv.xyx+vec3(66,2,4)) * huePower;

    // Output to screen
    return vec4(finalCol.rgb * Color, 1) * power;
}

