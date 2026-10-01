vec4 render(vec2 p) {
    for (float i = 1.; i < 20.; i++) {
        p.x += .5 / i * sin(i * p.y + time) + 1.;
        p.y += .5 / i * cos(i * p.x + time) + 2.;
    }

    p.y += cos(time / 4.) * 5.;
    p.x += sin(time / 3.) * 4.;

    vec3 col = vec3(abs(sin(.0 * p.x)) * 1.3, abs(sin(.5 * p.y)) + 0.3, abs(sin(1.0 * p.x + p.y)) + 0.3);
    float dist = sqrt(col.x * col.x + col.y * col.y + col.z * col.z);
    return vec4(col / dist, 1.0);
}
