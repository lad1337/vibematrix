//#version 330 core
// mod of https://there.oughta.be/an/led-cube shader
// environment: 0 = sandbox, 1 = desktop, 2 = raspberry pi
#ifndef environment
precision mediump float;
//#define simulation // activate to change load etc automatically
#define environment 0
#endif

// code with errors to check the preprocessor
/*
#if environment == 2
raspi;
#elif environment == 1
mac;
#elif environment == 0
sandbox;
#else
wtf;
#endif
*/

uniform float time;
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
float download = .1;
float upload = .1;
// last time data was updated in seconds
float age = 1.0;
#endif
#if environment == 1
uniform float p_factor;
#else
float p_factor = 0.5;
#endif

// const
vec3 mcolor = vec3(0.1, 0.6, 0.4);            // start color
const vec3 mcolorwarm = vec3(0.5, 0.5, 0.1);  // mid color
const vec3 mcolorhot = vec3(0.6, 0.2, 0.1);   // hight color
const vec3 ccolor = vec3(0.5, .4, 0.2);       // base mix color
const float color_pos_shift = 10.;            // start is compose of two color this moves it around
const vec3 ring_color = vec3(0.6, 0.6, 1.);
// smoke
const float zoom = 10.0;
const float inten = 1.;  // intensity
const int iter_n = 7;    // number of iterations

// am = amplituden modulation
// fm = frequency modulation
float circle(vec2 uv, float r, float width, float am, float fm) {
    float f = length(uv) + (sin(normalize(uv).y * am + time * fm) - sin(normalize(uv).x * am + time * fm)) / 100.0;
    float w = width + width * 0.1;
    return smoothstep(r - w, r, f) - smoothstep(r, r + w, f);
}

vec3 the_ring(vec2 p) {
    float am = 50.0 * (download + 0.01);
    float fm = 3.2 * (2. * upload + .7);
    float r = 0.2;

    float width = 0.005;
    float cResult = circle(p, r, width, am, fm);
    return ring_color * cResult;
}

vec3 smoke(vec2 p) {
    p = p * zoom;
    vec2 i = p;
    float c = 1.;

    for (int n = 0; n < iter_n; n++) {
        float t = time * (0.7 - (0.2 / float(n + 1)));
        i = p + vec2(cos(t - i.x) + sin(t + i.y), sin(t - i.y) + cos(t + i.x));
        c += 1.0 / length(vec2(p.x / (2.0 * sin(i.x + t) / inten), p.y / (cos(i.y + t) / inten)));
    }

    c /= float(iter_n);
    c = 1.5 - abs(c);
    mcolor.g = clamp(((abs(p.x) + abs(p.y)) / color_pos_shift) * (load * 0.5 + 0.1), 0.0, .9);
    mcolor += smoothstep(0.6, 0.2, load) * mcolor +
              smoothstep(0.2, 0.6, load) * smoothstep(1.0, 0.6, load) * mcolorwarm +
              smoothstep(0.6, 1.0, load) * mcolorhot;

    vec3 color = mcolor * c * c * c * c * ccolor;
    // the smoothstep removes the bright dot in the center
    return color * smoothstep(0.1, length(color), 1.);
}

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
    vec3 color = the_ring(p) * 1.5 + smoke(p);
    vec3 grayXfer = vec3(0.3, 0.59, 0.11);
    vec3 gray = vec3(dot(grayXfer, color));
    vec4 final_color = vec4(mix(color, gray, smoothstep(5.0, 10.0, age)), 1.0);

#if environment == 1
    FragColor = final_color;
#else
    gl_FragColor = final_color;
#endif
}
