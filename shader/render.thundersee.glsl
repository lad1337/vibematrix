#ifdef GL_ES

precision mediump float;
#endif
#define iTime time

float field(in vec3 p) {
    float strength = 7.6 + .03 * log(2.e-6 + fract(sin(iTime) * 5373.11));
    float accum = 0.;
    float prev = 1.;
    float tw = 0.5;
    for (int i = 0; i < 22; ++i) {
        float mag = dot(p, p);
        p = abs(p) / mag + vec3(-.5, -0.1, -1.5);
        float w = exp(-float(i) / 5.);
        accum += w * exp(-strength * pow(abs(mag - prev), 2.3));
        tw += w;
        prev = mag;
    }
    return max(0., 5. * accum / tw - .7);
}

vec4 render(vec2 p) {
    vec2 uv = p;
    vec2 uvs = uv * 2.5;
    vec3 pn = vec3(uvs / 4.5, 0) + vec3(1., -1.3, 0.);
    pn += .2 * vec3(sin(iTime / 10.5), sin(iTime / 5.), sin(iTime / 190.));
    float t = field(pn);
    float v = (1.111 - exp((abs(uv.x) - 1.6) * 6.)) * (1. - exp((abs(uv.y) - 1.) * 6.));
    return mix(14.4, 1.4, v) * vec4(1.4 * t * t * t, 1.4 * t * t, t, 2.0);
}

