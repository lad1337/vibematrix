// const
vec3 mcolor = vec3(0.1, 0.6, 0.4);            // start color
const vec3 mcolorwarm = vec3(0.5, 0.5, 0.1);  // mid color
const vec3 mcolorhot = vec3(0.6, 0.2, 0.1);   // hight color
const vec3 ccolor = vec3(0.5, .4, 0.2);       // base mix color
const float color_pos_shift = 3.5;            // start is compose of two color this moves it around
const vec3 ring_color = vec3(0.6, 0.6, 1.);
// smoke
const float zoom = 10.0;
const float inten = 1.;  // intensity
const int iter_n = 7;    // number of iterations

const int ITERATIONS = 10;
const float SPEED = 0.25;
const float PAN = 2.8;
const float MASK_VIS = 0.0;  // Set between 0-1 to visualize masks

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

float vortex(vec2 uv, float dist, float seed, float bias, float offset) {
    float ang = atan(uv.y, uv.x) + sin(dist + 0.1 * seed) * (1.2 - offset) * 2.0;
    ang += 3.14159 * (0.01 * seed);
    return clamp((sin((ang) * (3.0 + offset * float(ITERATIONS))) + bias) / (1.0 + bias), 0.0, 1.0);
}
vec3 particles() {
    vec2 uv = fragCoord;
    float texel = 1.0 / (length(uv.xy) * 30.0);
    float dist = length(uv);
    vec3 col = vec3(0.0);
    float iTime = time;
    for (int i = 0; i < ITERATIONS; i++) {
        float offset = float(i) / float(ITERATIONS);
        float seed = 1000.0 * fract(3.1379136 * floor(iTime * SPEED + offset));
        float time = fract(iTime * SPEED + offset);
        vec2 pan = vec2(0.0, time * PAN);
        float maskA = vortex(uv, dist, seed + 100.0 * float(i), -0.998 + texel, offset);
        float dist2 = length(uv + pan);
        float maskB = vortex(uv + pan, dist2, seed + 42.0 * float(i), -0.99 + texel, offset);
        float radius = pow(((maskA * maskA) + (maskB * maskB)), 2.0);
        float fade = time * (1.0 - time);
        float mask = maskA * maskB * fade * radius;
        col = mix(
            col,
            vec3(1.50 + 2.0 * dist - 1.2 * offset - 1.1 * radius, 0.75 - 0.5 * offset - 0.5 * dist, 1.0 - 0.8 * dist),
            mask);
        col = max(
            vec3(maskA * MASK_VIS, maskB * MASK_VIS + 0.1, max(maskA * MASK_VIS * 2.0, maskB * MASK_VIS) + 0.12) * fade,
            col);
    }
    return vec3(col * 1.0);
}

vec4 render(vec2 p) {
    // vec3 color = mix(smoke(p), particles() / 10000.0, 0.0);
    // vec3 color = particles();
    vec3 color = smoke(p);
    vec3 grayXfer = vec3(0.3, 0.59, 0.11);
    vec3 gray = vec3(dot(grayXfer, color));
    return vec4(mix(color, gray, smoothstep(5.0, 10.0, age)), 1.0);
}
