vec3 destColor;

vec4 circle(vec2 p, float scale) {
    float l = 0.001 / abs(length(p * scale) - 0.1);
    return vec4(l / destColor, 1.0);
}

vec4 render(vec2 p) {
    p *= 1.5;
    destColor = vec3(1, 1, 0.5);
    vec4 col = vec4(0.0);
    float scale = 2.4;
    for (int i = 0; i < 14; ++i) {
        if (mod(float(i), 2.) > 0.)
            col.rgba += vec4(circle(p, 0.2 * scale));
        else
            col.rgba += vec4(circle(p, 0.2 * scale));
        scale *= 0.9;
        p.x += sin(time * 1.) * .015;
        p.y += cos(time * 1.) * .012;
        col.r *= float(i) / 10.;
        col.g *= float(i) / 14.;
        col.b *= float(i) / 10. * sin(time * 0.2);
    }
    return col;
}
