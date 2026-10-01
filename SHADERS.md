# Custom shaders

A vibematrix shader is one GLSL file that defines a single function:

```glsl
vec4 render(vec2 p)
```

vibematrix calls it once per pixel, every frame. You return a colour, and the
file-change signals arrive as uniforms. This is the same format as the
`render.*.glsl` files of the [LED cube](https://github.com/lad1337/cube), so
shaders work in both places (see [On the cube](#on-the-cube)).

## Loading one

Pass a file path anywhere a shader name goes:

```sh
vibematrix --shader ~/shaders/pulse.glsl              # command line
VIBEMATRIX_SHADER=~/shaders/pulse.glsl vibematrix     # environment
echo 'shader = ~/shaders/pulse.glsl' >> ~/.config/vibematrix/config   # default
```

A value that contains a `/` or ends in `.glsl` is a file. Anything else is the
name of a built-in shader (`vibematrix --help` lists them). `~/` is expanded,
and relative paths are relative to the current directory, so use absolute or
`~/` paths in the config file.

**Live reload:** vibematrix checks the file twice a second and recompiles it
when it changes, so you can keep it running while you edit. If a save doesn't
compile, the last working version keeps running and the header turns red with
the first error, e.g. `pulse.glsl:7: Use of undeclared identifier 'colr'`.
Line numbers are lines of your file.

## The contract

| | |
|---|---|
| **Define** | `vec4 render(vec2 p)`. Helper functions, `const`s and `#define`s above it are fine. |
| **Don't define** | `main()`, `#version`, or the uniforms below. vibematrix adds all of these, and declaring them again is a compile error. |
| **`p`** | Pixel position. `(0, 0)` is the centre and y points **up**. `p.y` runs from −0.5 (bottom) to 0.5 (top). `p.x` runs from −0.5·aspect to 0.5·aspect, where aspect is the field's width / height in pixels, so circles stay round. |
| **Pixel** | Half a character cell: each cell shows two pixels stacked (`▀`). |
| **Return** | Colour as `vec4(r, g, b, a)`, each channel 0..1, clamped. Alpha is ignored. |

### Uniforms

All are `float`, except where noted.

| name | value |
|---|---|
| `time` (alias `iTime`) | seconds since start |
| `load` | files changed per second, 0..1 |
| `download` | lines added per second, 0..1 |
| `upload` | lines removed per second, 0..1 |
| `age` | seconds since the last change. Starts at 60, so a fresh start looks idle. |
| `resolution` (`vec2`) | field size in pixels |
| `mouse` (`vec2`) | always `(0.5, 0.5)`, kept for glslsandbox-style code |
| `p_factor` | 0.5, already applied to `p` |

`load`, `download` and `upload` are log-scaled and smoothed. They glide, they
never jump, and they change by at most 0.5 per second, so you can use them
directly for speed, size or colour without your own smoothing. `age` is the
one value that resets instantly; it's useful for "fade out when idle", like
`smoke2`'s `clamp(age / 10., 0., 1.)`.

### GLSL dialect

- It compiles as **GLSL 3.30 core**: `#version 330 core` is added for you.
- `precision` statements are accepted (they're no-ops).
- `#extension` lines are removed. The GLES ones the cube shaders use are core
  features here anyway.
- `texture2D` is mapped to `texture`, but **no textures are bound**, so don't
  sample anything.
- `gl_FragColor` doesn't exist: return your colour from `render` instead.

## Example

```glsl
// pulse.glsl: rings from the centre, faster with activity, grey when idle
vec4 render(vec2 p) {
    float r = length(p);
    float rings = .5 + .5 * sin(r * 40. - time * (2. + 10. * load));
    vec3 col = vec3(.15 + .85 * download, .25, .15 + .85 * upload) * rings;
    float idle = clamp(age / 10., 0., 1.);
    col = mix(col, vec3(dot(col, vec3(.3, .6, .1))), idle);
    return vec4(col, 1.);
}
```

Save it as `~/shaders/pulse.glsl` and run `vibematrix --shader ~/shaders/pulse.glsl`.
Run `./demo.sh` in another terminal to see it react without touching real files.

## On the cube

The cube (`cpu-stats-gl.cpp`) runs OpenGL ES 2.0 on a Raspberry Pi. To use the
same file on both:

- Stick to GLSL ES 1.00. Loops need constant bounds, there's no `texture()`,
  and float literals need a dot (`1.`, not `1`).
- Don't use `resolution` or `mouse`: the cube doesn't set them.
- Copy the file to the cube repo's `shader/render.NAME.glsl` and start the cube
  with `--render shader/render.NAME.glsl`.

On the cube the same uniforms carry CPU load and network traffic, so a shader
written for file activity also reacts to system activity there.
