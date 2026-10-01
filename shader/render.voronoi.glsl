// Modified from: https://www.shadertoy.com/view/MslGD8

// Created by inigo quilez - iq/2013
// License Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License.

#ifdef GL_ES
precision mediump float;
#endif

#extension GL_OES_standard_derivatives : enable

vec2 hash(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return fract(sin(p) * 18.5453);
}

// return distance, and cell id
vec2 voronoi(in vec2 x) {
    vec2 n = floor(x);
    vec2 f = fract(x);

    vec3 m = vec3(9.0);
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++) {
            vec2 g = vec2(float(i), float(j));
            vec2 o = hash(n + g);
            vec2 r = g - f + (0.5 + 0.5 * sin(time + 6.2831 * o));
            float d = dot(r, r);
            if (d < m.x) m = vec3(d, o);
        }

    return vec2(sqrt(m.x), m.y + m.z);
}

vec4 render(vec2 p) {
    // computer voronoi patterm
    vec2 c = voronoi((14.0 + 6.0 * sin(0.2 * time)) * p);
    // colorize
    vec3 colorConfig = vec3(-1.0, .0, 2.0);  // colorfull
    // vec3 colorConfig = vec3(0.0, 1.0, 2.0); // normal
    vec3 col = 0.5 + 0.5 * cos(c.y * 6.2831 + colorConfig);

    return vec4(col, 1.0);
}
