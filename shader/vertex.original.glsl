//#version 330 core
// environment: 0 = sandbox, 1 = desktop, 2 = raspberry pi
#ifndef environment
precision mediump float;
//#define simulation // activate to change load etc automatically
#define environment 0
#endif

#if environment == 1
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 coord;
out vec2 fragCoord;
#else
attribute vec3 aPos;
attribute vec2 coord;
varying vec2 fragCoord;
#endif

void main() {
    fragCoord = coord;
    gl_Position = vec4(aPos, 1.0);
}
