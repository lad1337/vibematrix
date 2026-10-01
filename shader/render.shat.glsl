float getGas(vec2 p) {
    return (cos(p.y * 40.0 + time * 0.1) + 1.0) * 0.5 + (sin(p.x * 20.0 + time * 0.1) + 1.0) * 0.0 + 0.1;
}

vec4 render(vec2 p) {
    for (int i = 1; i < 10; i++) {
        vec2 newp = p;
        newp.x += (0.4 / (float(i))) * (sin(p.y * (10.0 + time * 0.0001)) * 0.2 * sin(p.x * 30.0) * 0.8);
        newp.y += (0.4 / (float(i))) * (cos(p.x * (20.0 + time * 0.0001)) * 0.2 * sin(p.x * 5.0) + time * 0.1);
        p = newp * 0.95;
    }

    vec3 clr = vec3(0.1, 0.3, 0.1);
    clr /= getGas(p);
    clr /= getGas(p * 1.4);

    return vec4(clr * 0.4, 1.0);
}
