# vibematrix

A music-visualizer for your files. vibematrix watches a directory and plays the
[LED cube](https://github.com/lad1337/cube) GLSL shaders full-screen in your
terminal, driven by what changes: new files, edited lines, deletions. Quiet
directory, calm grey smoke. Busy directory, colour and motion.

macOS only (offscreen OpenGL via CGL, file events via FSEvents). Needs a
truecolor terminal. Ghostty, kitty, iTerm2 and WezTerm work well; Terminal.app
works but slower.

## Install

### Homebrew

```sh
brew tap lad1337/vibematrix https://github.com/lad1337/vibematrix
brew install lad1337/vibematrix/vibematrix
```

Latest `main` instead of the release: `brew install --HEAD lad1337/vibematrix/vibematrix`.

### From source

```sh
git clone https://github.com/lad1337/vibematrix
cd vibematrix
make            # builds ./vibematrix (Xcode command line tools, no other deps)
make test       # self-test: line diffing, smoothing, all shaders compile
make install    # /usr/local by default, or: make install PREFIX=~/.local
```

`make uninstall` removes it again. A plain `make` build runs in place: it finds
`shader/` next to the binary.

## Usage

```sh
vibematrix                     # watch the current directory
vibematrix ~/code/project      # watch a directory
vibematrix . --shader oil      # pick a shader
vibematrix --shader x          # unknown name: lists the available shaders
```

Quit with `q` or `Esc`.

The top line shows the shader, the watched path, the file count and the live
shader inputs. The bottom lines show the latest changes, e.g.
`~ src/main.c  (+12 -3 lines)`.

`.git`, `node_modules`, `__pycache__`, `.venv` and `.idea` are ignored.

## Demo / recording

`demo.sh` generates endless file activity, so you can watch or record vibematrix
without touching real files:

```sh
./demo.sh /tmp/demo          # terminal 1: keeps changing files in /tmp/demo
vibematrix /tmp/demo         # terminal 2: record this one
```

One round takes about 45 seconds, then repeats: quiet (fades to grey), a
trickle of small edits, a burst of new files, a refactor, deletions, and one
20,000-line file that appears and then goes away. `SPEED=2 ./demo.sh …` runs it
twice as fast. Without a directory argument it uses a temporary one and removes
it on Ctrl-C.

## How changes drive the shaders

The shaders take the same uniforms as on the cube, where they come from CPU and
network stats. Here they come from files:

| uniform    | on the cube        | in vibematrix                     |
|------------|--------------------|-----------------------------------|
| `load`     | CPU load           | files changed per second          |
| `download` | bytes/s in         | lines added per second            |
| `upload`   | bytes/s out        | lines removed per second          |
| `age`      | s since last data  | seconds since the last change     |
| `time`     | clock              | clock                             |

- **Lines are compared by content.** Editing a line counts as 1 removed + 1 added.
  A new file adds all its lines, and a deleted file removes them. Binary files
  count in 64-byte chunks. Files over 8 MB only count toward `load`.
- **Log scaled.** A single save is visible, and a huge refactor saturates
  instead of clipping.
- **Smoothed, never jumping.** Each value follows a critically damped spring
  with a speed limit of 0.5 per second, so even a burst of thousands of files
  ramps in over a second or two and fades back out.
- **`age`:** the default `smoke2` shader fades to grey as `age` grows.

## Shaders

All shaders from the cube repo are in `shader/`: `2colerSmoke`, `art`,
`blue-ocean`, `clouds`, `clouds2`, `oil`, `rainbow-wave`, `shat`, `smoke`,
`smoke2` (default), `smoke2.1`, `star`, `thundersee`, `tunnel` and `voronoi`.
`flight` is broken upstream: its `render()` never returns a value.

To add your own, drop a `render.NAME.glsl` into `shader/` that defines
`vec4 render(vec2 p)`. It is spliced into `fragment.template.glsl`, exactly as
on the cube. Then run `vibematrix --shader NAME`.

## Tuning

The constants at the top of `vibematrix.c` control the behaviour:

| constant      | what it controls |
|---------------|------------------|
| `FPS`         | frame rate cap |
| `TIME_SPEED`  | how fast shader time runs |
| `FILES_MAX`, `LINES_MAX`     | rate that maxes out a uniform |
| `FILES_FLOOR`, `LINES_FLOOR` | rate that counts as silence |
| `RATE_WINDOW` | how long activity lingers |
| `SPRING_W`, `MAX_SLEW`       | smoothing |
| `TOL`         | how much a cell's colour must change before it is redrawn (less terminal traffic) |
