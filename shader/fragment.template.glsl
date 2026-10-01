// environment: 0 = sandbox, 1 = desktop, 2 = raspberry pi

//#version 330 core
// mod of https://there.oughta.be/an/led-cube shader
#ifndef environment
#define environment 0
#endif

#if environment == 0
precision mediump float;
#endif

uniform float time;
#define iTime time
#if environment == 1
out vec4 FragColor;
in vec2 fragCoord;
#elif environment == 0
uniform vec2 resolution;
#else
varying vec2 fragCoord;
#endif

#if environment > 0
// all expected to the within 0.0 & 1.0
uniform float load;
uniform float download;
uniform float upload;
// last time data was updated in seconds
uniform float age;
#else
// all expected to the within 0.0 & 1.0
float load = 0.1;
float download = .0;
float upload = .0;
// last time data was updated in seconds
float age = .2;
#endif
#if environment == 1
uniform float p_factor;
#else
float p_factor = 0.5;
#endif

// RENDER STARTS HERE
// RENDER ENDS HERE
void main(void) {
#if environment != 0
    vec2 p = fragCoord.xy * p_factor;
#else
    vec2 p = (gl_FragCoord.xy - 0.5 * resolution.xy) / resolution.y;
#endif

#ifdef simulation
    load = abs(sin(time * 0.2));
    download = abs(sin(time * 0.1));
    upload = abs(sin(time * 0.01));
#endif

#if environment == 1
    FragColor = render(p);
#else
    gl_FragColor = render(p);
#endif
}
