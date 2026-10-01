// vibematrix: run the LED cube GLSL shaders (github.com/lad1337/cube) in the terminal,
// driven by file changes under a directory instead of CPU/network stats.
//
// Shader uniforms (cube's cpu-stats-gl.cpp names, 0..1, log scaled, spring smoothed):
//   load      files changed per second   (added, modified, deleted)
//   download  lines added per second     (a new file adds all its lines)
//   upload    lines removed per second   (a deleted file removes all its lines)
//   age       seconds since last change  (smoke2 fades to grey when idle)
// Lines are compared as multisets of line hashes, so an edited line = 1 removed + 1 added.
// Smoothing lives here, not in GLSL: shaders keep no state between frames, and this way
// every cube shader gets it unchanged (like cube's ANIMSTEP on the CPU side).
//
// build: make      run: ./vibematrix [DIR] [--shader NAME]   (q / esc quits)   test: ./vibematrix --test
// settings, later wins: built-in default < config file < environment < command line
//   config file  $XDG_CONFIG_HOME/vibematrix/config (else ~/.config/vibematrix/config), `key = value`
//   shader       config `shader = oil`   env VIBEMATRIX_SHADER=oil   arg --shader oil
// macOS only: CGL offscreen OpenGL + FSEvents.
#define GL_SILENCE_DEPRECATION
#include <CoreServices/CoreServices.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <dispatch/dispatch.h>
#include <fcntl.h>
#include <fts.h>
#include <libgen.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_SHADER "smoke2" // same default as cube's cpu-stats-gl.cpp
#ifndef VERSION
#define VERSION "dev" // make passes `git describe`, brew passes the formula version
#endif
#define FPS 30
#define TIME_SPEED 1.0     // shader `time` per second
#define FILES_MAX 8.0      // files/s for load = 1 (log scale: 1 file/s ~ 0.45)
#define FILES_FLOOR 0.25   // files/s that count as ~silence
#define LINES_MAX 5000.0   // lines/s for download/upload = 1
#define LINES_FLOOR 2.0    // lines/s that count as ~silence
#define RATE_WINDOW 2.0    // s, decay time constant of the per-second rates
#define SPRING_W 2.5       // rad/s, smoothing spring stiffness: settles in ~2s, never overshoots
#define MAX_SLEW 0.5       // max change per second of any uniform: a burst can't jump
#define MAX_HASH_BYTES (8 << 20) // ponytail: bigger files count as a file change with 0 lines
#define CHUNK 64           // binary files: 64-byte chunks stand in for lines
#define LOG_LINES 4
#ifndef TOL
#define TOL 3              // don't redraw a cell whose colour moved less than this (0..255); 0 = always
#endif
#define FS_LATENCY 0.05    // s, FSEvents batching

static const char *SKIP[] = {".git", "node_modules", "__pycache__", ".venv", ".idea", NULL};

// environment 1 = cube's desktop path (tut.cpp). The extras cover what some render.*.glsl
// expect from glslsandbox (resolution, mouse, texture2D).
static const char *HEADER = "#version 330 core\n#define environment 1\n";
static const char *FRAG_EXTRA = "uniform vec2 resolution;\nuniform vec2 mouse;\n#define texture2D texture\n";

static void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

// ------------------------------------------------------------ file state: path -> size, line hashes

typedef struct {
    char *path;
    long long size;  // < 0: not present (entries are never really deleted)
    uint64_t *lines; // sorted line hashes
    size_t nlines;
} Entry;
static Entry *tab;
static size_t cap, used;

static uint64_t fnv(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    while (*s) h = (h ^ (unsigned char)*s++) * 1099511628211ULL;
    return h;
}

static uint64_t fnvn(const char *s, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    while (n--) h = (h ^ (unsigned char)*s++) * 1099511628211ULL;
    return h;
}

static int cmp64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

// sorted hashes of the file's lines (newline excluded, so appending to a last line without
// '\n' is 1 changed line); binary files are cut into CHUNK-byte pieces instead
static size_t hash_lines(const char *path, long long size, uint64_t **out) {
    *out = NULL;
    FILE *f = size > 0 && size <= MAX_HASH_BYTES ? fopen(path, "rb") : NULL;
    if (!f) return 0;
    char *buf = malloc(size);
    size_t n = fread(buf, 1, size, f), k = 0, cap = 64;
    fclose(f);
    int binary = memchr(buf, 0, n < 8192 ? n : 8192) != NULL;
    uint64_t *h = malloc(cap * sizeof *h);
    for (size_t i = 0, j; i < n; i = j) {
        const char *nl = binary ? NULL : memchr(buf + i, '\n', n - i);
        j = binary ? (i + CHUNK < n ? i + CHUNK : n) : nl ? (size_t)(nl - buf) + 1 : n;
        if (k == cap) h = realloc(h, (cap *= 2) * sizeof *h);
        h[k++] = fnvn(buf + i, j - i - (nl != NULL));
    }
    free(buf);
    qsort(h, k, sizeof *h, cmp64);
    *out = h;
    return k;
}

// multiset difference of two sorted hash lists
static void line_diff(const uint64_t *a, size_t na, const uint64_t *b, size_t nb, long *added, long *removed) {
    size_t i = 0, j = 0;
    *added = *removed = 0;
    while (i < na || j < nb) {
        if (j == nb || (i < na && a[i] < b[j])) (*removed)++, i++;
        else if (i == na || b[j] < a[i]) (*added)++, j++;
        else i++, j++;
    }
}

static Entry *slot(const char *p) {
    size_t i = fnv(p) & (cap - 1);
    while (tab[i].path && strcmp(tab[i].path, p)) i = (i + 1) & (cap - 1);
    return &tab[i];
}

static Entry *entry(const char *p) {
    if ((used + 1) * 10 > cap * 7) { // grow at 70% load
        Entry *old = tab;
        size_t oldcap = cap;
        cap = cap ? cap * 2 : 4096;
        tab = calloc(cap, sizeof *tab);
        for (size_t i = 0; i < oldcap; i++)
            if (old[i].path) *slot(old[i].path) = old[i];
        free(old);
    }
    Entry *e = slot(p);
    if (!e->path) {
        e->path = strdup(p);
        e->size = -1;
        used++;
    }
    return e;
}

static int skipped(const char *p) {
    size_t n = strlen(p);
    for (const char **s = SKIP; *s; s++) {
        char pat[64];
        size_t m = strlen(*s);
        snprintf(pat, sizeof pat, "/%s/", *s);
        if (strstr(p, pat) || (n > m && p[n - m - 1] == '/' && !strcmp(p + n - m, *s))) return 1;
    }
    return 0;
}

// ------------------------------------------------------------ events shared with the FSEvents queue

// The path table (entry/tab) is only touched from the serial FS queue, so it needs no lock.
// `mu` guards just `shared`, and is never held across file I/O: the render loop takes it
// every frame and must never wait on the disk (that froze the UI, q and signals).
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static char root[PATH_MAX];
static struct {
    double files_ev, added, removed; // accumulated since the main loop last took them
    int any, scanning;
    long files;
    struct { char kind; char text[400]; } log[LOG_LINES];
} shared;

// classify by what is on disk now vs what we knew: robust to FSEvents coalescing flags
static void on_path(const char *p) {
    if (skipped(p)) return;
    struct stat st;
    int exists = !lstat(p, &st) && S_ISREG(st.st_mode);
    Entry *e = entry(p);
    char kind;
    long add, del, dfiles = 0;
    if (exists) {
        uint64_t *h;
        size_t n = hash_lines(p, st.st_size, &h); // file I/O: outside the lock
        kind = e->size < 0 ? '+' : '~';
        line_diff(e->lines, e->nlines, h, n, &add, &del); // new file: everything is added
        dfiles = e->size < 0;
        free(e->lines);
        e->lines = h, e->nlines = n, e->size = st.st_size;
    } else if (e->size >= 0) {
        kind = '-';
        add = 0, del = (long)e->nlines;
        free(e->lines);
        e->lines = NULL, e->nlines = 0, e->size = -1;
        dfiles = -1;
    } else {
        return; // came and went between batches
    }
    size_t rl = strlen(root);
    const char *rel = !strncmp(p, root, rl) && p[rl] == '/' ? p + rl + 1 : p;
    pthread_mutex_lock(&mu);
    shared.files += dfiles;
    shared.files_ev++;
    shared.added += add;
    shared.removed += del;
    shared.any = 1;
    memmove(shared.log, shared.log + 1, sizeof shared.log[0] * (LOG_LINES - 1));
    shared.log[LOG_LINES - 1].kind = kind;
    snprintf(shared.log[LOG_LINES - 1].text, sizeof shared.log[0].text, "%c %s  (+%ld -%ld lines)", kind, rel, add, del);
    pthread_mutex_unlock(&mu);
}

static void fs_cb(ConstFSEventStreamRef s, void *info, size_t n, void *paths,
                  const FSEventStreamEventFlags flags[], const FSEventStreamEventId ids[]) {
    (void)s, (void)info, (void)flags, (void)ids;
    // ponytail: kFSEventStreamEventFlagMustScanSubDirs (dropped events) is ignored; rescan there if it bites
    for (size_t i = 0; i < n; i++) on_path(((char **)paths)[i]);
}

static void scan(void) {
    char *argv[] = {root, NULL};
    FTS *f = fts_open(argv, FTS_PHYSICAL | FTS_NOCHDIR, NULL);
    FTSENT *e;
    while (f && (e = fts_read(f))) {
        if (e->fts_info == FTS_D && e->fts_level > 0 && skipped(e->fts_path)) fts_set(f, e, FTS_SKIP);
        else if (e->fts_info == FTS_F) {
            Entry *x = entry(e->fts_path);
            x->size = e->fts_statp->st_size;
            x->nlines = hash_lines(e->fts_path, x->size, &x->lines); // file I/O: outside the lock
            pthread_mutex_lock(&mu);
            shared.files++;
            pthread_mutex_unlock(&mu);
        }
    }
    if (f) fts_close(f);
}

static void watch(void) {
    // hash the tree on the FS queue, so the animation starts right away on big trees
    dispatch_queue_t q = dispatch_queue_create("vibematrix.fs", NULL);
    shared.scanning = 1; // queue not started yet: no lock needed
    dispatch_async(q, ^{
        scan();
        pthread_mutex_lock(&mu);
        shared.scanning = 0;
        pthread_mutex_unlock(&mu);
    });
    CFStringRef cfroot = CFStringCreateWithCString(NULL, root, kCFStringEncodingUTF8);
    CFArrayRef paths = CFArrayCreate(NULL, (const void **)&cfroot, 1, &kCFTypeArrayCallBacks);
    FSEventStreamRef st = FSEventStreamCreate(NULL, fs_cb, NULL, paths, kFSEventStreamEventIdSinceNow, FS_LATENCY,
                                              kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer);
    FSEventStreamSetDispatchQueue(st, q); // serial: events wait for the scan
    if (!FSEventStreamStart(st)) die("could not watch %s", root);
}

// ------------------------------------------------------------ events -> eased uniforms

typedef struct { double x, v; } Spring;
typedef struct {
    double files, added, removed, last; // decaying sums, /RATE_WINDOW = per second
    Spring load, download, upload;
} Sig;

// critically damped spring with a speed limit: starts and stops gently, never overshoots,
// and no burst can move a uniform faster than MAX_SLEW per second
static void follow(Spring *s, double target, double dt) {
    s->v += (SPRING_W * SPRING_W * (target - s->x) - 2 * SPRING_W * s->v) * dt;
    s->v = fmax(-MAX_SLEW, fmin(MAX_SLEW, s->v));
    s->x += s->v * dt;
}

static double lognorm(double x, double floor, double full) {
    double v = log1p(fmax(0, x) / floor) / log1p(full / floor);
    return v > 1 ? 1 : v;
}

static void sig_step(Sig *s, double dt) {
    for (; dt > 0; dt -= 1.0 / 120) { // fixed substeps: same motion at any frame rate
        double h = fmin(dt, 1.0 / 120), k = exp(-h / RATE_WINDOW);
        s->files *= k, s->added *= k, s->removed *= k;
        follow(&s->load, lognorm(s->files / RATE_WINDOW, FILES_FLOOR, FILES_MAX), h);
        follow(&s->download, lognorm(s->added / RATE_WINDOW, LINES_FLOOR, LINES_MAX), h);
        follow(&s->upload, lognorm(s->removed / RATE_WINDOW, LINES_FLOOR, LINES_MAX), h);
    }
}

// ------------------------------------------------------------ shaders

static char shader_dir[PATH_MAX];

static char *slurp(const char *dir, const char *name) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *s = malloc(n + 1);
    s[fread(s, 1, n, f)] = 0;
    fclose(f);
    return s;
}

typedef struct {
    GLuint prog, vao, vbo, fbo, rb;
    int w, h;
    GLint u_time, u_load, u_down, u_up, u_age, u_pf, u_res, u_mouse;
} Shader;

static GLuint compile(GLenum type, const char *src, char *err, size_t errn) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glGetShaderInfoLog(s, (GLsizei)errn, NULL, err);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

// assemble like cpu-stats-gl.cpp: header + template with render.NAME.glsl spliced in at the marker
static int shader_load(Shader *sh, const char *name, char *err, size_t errn) {
    char fname[256];
    snprintf(fname, sizeof fname, "render.%s.glsl", name);
    char *vert = slurp(shader_dir, "vertex.original.glsl"), *tmpl = slurp(shader_dir, "fragment.template.glsl"),
         *body = slurp(shader_dir, fname);
    if (!vert || !tmpl || !body) return snprintf(err, errn, "missing shader files in %s", shader_dir), 0;
    char *marker = strstr(tmpl, "// RENDER ENDS HERE");
    if (!marker) return snprintf(err, errn, "no RENDER ENDS HERE marker in template"), 0;
    // #extension must precede code in GLSL 330; the GLES ones these use are core anyway
    for (char *l = body; *l;) {
        char *nl = strchr(l, '\n'), *t = l;
        while (*t == ' ' || *t == '\t') t++;
        if (!strncmp(t, "#extension", 10)) memset(l, ' ', (nl ? nl : l + strlen(l)) - l);
        l = nl ? nl + 1 : l + strlen(l);
    }
    char *vs, *fs;
    asprintf(&vs, "%s%s", HEADER, vert);
    asprintf(&fs, "%s%s%.*s%s%s", HEADER, FRAG_EXTRA, (int)(marker - tmpl), tmpl, body, marker);
    free(vert), free(tmpl), free(body);

    memset(sh, 0, sizeof *sh);
    GLuint v = compile(GL_VERTEX_SHADER, vs, err, errn), f = v ? compile(GL_FRAGMENT_SHADER, fs, err, errn) : 0;
    free(vs), free(fs);
    if (!f) return 0;
    sh->prog = glCreateProgram();
    glAttachShader(sh->prog, v), glAttachShader(sh->prog, f);
    glLinkProgram(sh->prog);
    glDeleteShader(v), glDeleteShader(f);
    GLint ok;
    glGetProgramiv(sh->prog, GL_LINK_STATUS, &ok);
    if (!ok) return glGetProgramInfoLog(sh->prog, (GLsizei)errn, NULL, err), 0;
    // -1 for uniforms a shader doesn't use; glUniform ignores -1
    sh->u_time = glGetUniformLocation(sh->prog, "time");
    sh->u_load = glGetUniformLocation(sh->prog, "load");
    sh->u_down = glGetUniformLocation(sh->prog, "download");
    sh->u_up = glGetUniformLocation(sh->prog, "upload");
    sh->u_age = glGetUniformLocation(sh->prog, "age");
    sh->u_pf = glGetUniformLocation(sh->prog, "p_factor");
    sh->u_res = glGetUniformLocation(sh->prog, "resolution");
    sh->u_mouse = glGetUniformLocation(sh->prog, "mouse");
    return 1;
}

// RGB, rows bottom-up, into px (w*h*3)
static void shader_render(Shader *s, int w, int h, float t, const Sig *g, float age, unsigned char *px) {
    if (s->w != w || s->h != h) {
        s->w = w, s->h = h;
        float a = (float)w / h; // coord: y in -1..1, x aspect-correct; template scales by p_factor
        float v[] = {-1, -1, 0, -a, -1, 1, -1, 0, a, -1, -1, 1, 0, -a, 1, 1, 1, 0, a, 1};
        if (!s->vao) {
            glGenVertexArrays(1, &s->vao), glGenBuffers(1, &s->vbo);
            glGenFramebuffers(1, &s->fbo), glGenRenderbuffers(1, &s->rb);
        }
        glBindVertexArray(s->vao);
        glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); // vertex.original.glsl: layout(location = 0) aPos, 1 coord
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(3 * sizeof(float)));
        glBindRenderbuffer(GL_RENDERBUFFER, s->rb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
        glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, s->rb);
    }
    glUseProgram(s->prog);
    glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
    glBindVertexArray(s->vao);
    glViewport(0, 0, w, h);
    glUniform1f(s->u_time, t);
    glUniform1f(s->u_load, (float)g->load.x);
    glUniform1f(s->u_down, (float)g->download.x);
    glUniform1f(s->u_up, (float)g->upload.x);
    glUniform1f(s->u_age, age);
    glUniform1f(s->u_pf, 0.5f);
    glUniform2f(s->u_res, (float)w, (float)h);
    glUniform2f(s->u_mouse, 0.5f, 0.5f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);
}

static int gl_init(void) {
    CGLPixelFormatAttribute at[] = {kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core, 0};
    CGLPixelFormatObj pf;
    CGLContextObj ctx;
    GLint n;
    if (CGLChoosePixelFormat(at, &pf, &n) || !pf) return 0;
    int ok = !CGLCreateContext(pf, NULL, &ctx);
    CGLDestroyPixelFormat(pf);
    return ok && !CGLSetCurrentContext(ctx);
}

static int is_shader(const char *f) {
    size_t n = strlen(f);
    return n > 12 && !strncmp(f, "render.", 7) && !strcmp(f + n - 5, ".glsl");
}

static int shader_exists(const char *name) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/render.%s.glsl", shader_dir, name);
    return !access(path, R_OK);
}

static void list_shaders(FILE *out) {
    DIR *d = opendir(shader_dir);
    struct dirent *e;
    while (d && (e = readdir(d)))
        if (is_shader(e->d_name)) fprintf(out, " %.*s", (int)strlen(e->d_name) - 12, e->d_name + 7);
    if (d) closedir(d);
    fputc('\n', out);
}

// ------------------------------------------------------------ terminal

typedef struct { char *p; size_t n, cap; } Buf;

static void bput(Buf *b, const char *fmt, ...) {
    va_list ap;
    for (;;) {
        va_start(ap, fmt);
        int k = vsnprintf(b->p + b->n, b->cap - b->n, fmt, ap);
        va_end(ap);
        if (k >= 0 && b->n + k < b->cap) return (void)(b->n += k);
        b->cap = (b->cap + k + 1) * 2;
        b->p = realloc(b->p, b->cap);
    }
}

static void write_all(const char *p, size_t n) {
    while (n) {
        ssize_t k = write(1, p, n);
        if (k <= 0) return;
        p += k, n -= k;
    }
}

static int near(const unsigned char *a, const unsigned char *b) {
    return abs(a[0] - b[0]) <= TOL && abs(a[1] - b[1]) <= TOL && abs(a[2] - b[2]) <= TOL;
}

// Two pixels per cell ('▀': fg = upper, bg = lower). Only cells that changed are sent, and colour
// codes only when they differ from the previous cell: this is what keeps the terminal fast.
static void draw_field(Buf *b, const unsigned char *px, unsigned char *prev, int cols, int frows, int full) {
    int h = frows * 2, have_fg = 0, have_bg = 0;
    unsigned char fg[3], bg[3];
    for (int row = 0; row < frows; row++) {
        int cursor_ok = 0;
        const unsigned char *top = px + (size_t)(h - 1 - 2 * row) * cols * 3, *bot = top - (size_t)cols * 3;
        for (int x = 0; x < cols; x++, top += 3, bot += 3) {
            unsigned char *pv = prev + ((size_t)row * cols + x) * 6;
            if (!full && near(pv, top) && near(pv + 3, bot)) {
                cursor_ok = 0;
                continue;
            }
            memcpy(pv, top, 3), memcpy(pv + 3, bot, 3);
            if (!cursor_ok) bput(b, "\x1b[%d;%dH", row + 2, x + 1), cursor_ok = 1;
            if (!have_fg || memcmp(fg, top, 3)) bput(b, "\x1b[38;2;%d;%d;%dm", top[0], top[1], top[2]), memcpy(fg, top, 3), have_fg = 1;
            if (!have_bg || memcmp(bg, bot, 3)) bput(b, "\x1b[48;2;%d;%d;%dm", bot[0], bot[1], bot[2]), memcpy(bg, bot, 3), have_bg = 1;
            bput(b, "\xe2\x96\x80");
        }
    }
}

static struct termios saved_term;
static int out_flags;
static volatile sig_atomic_t quit;
// First signal asks the loop to quit cleanly. A second one means it didn't: restore the
// terminal and leave right here (only async-signal-safe calls).
static void on_signal(int s) {
    if (quit) {
        static const char reset[] = "\x1b[0m\x1b[?25h\x1b[?1049l";
        fcntl(1, F_SETFL, out_flags);
        (void)!write(1, reset, sizeof reset - 1);
        tcsetattr(0, TCSANOW, &saved_term);
        _exit(128 + s);
    }
    quit = 1;
}

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void run(const char *name) {
    if (!gl_init()) die("no OpenGL 3.2 core context available");
    Shader sh;
    char err[2048];
    if (!shader_load(&sh, name, err, sizeof err)) die("shader %s: %s", name, err);
    // without a terminal there is nobody to quit it and nothing to draw on
    if (!isatty(0) || !isatty(1)) die("vibematrix needs a terminal on stdin and stdout");
    watch();

    struct termios raw;
    tcgetattr(0, &saved_term);
    raw = saved_term;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0, raw.c_cc[VTIME] = 0; // non-blocking reads
    tcsetattr(0, TCSANOW, &raw);
    signal(SIGINT, on_signal), signal(SIGTERM, on_signal), signal(SIGHUP, on_signal);
    write_all("\x1b[?1049h\x1b[?25l\x1b[2J", 19);

    Buf b = {0};
    unsigned char *px = NULL, *prev = NULL;
    int cols = 0, rows = 0;
    Sig g = {0};
    double t = 0, last = now_s();
    g.last = last - 60; // start idle (grey in smoke2)
    struct { char kind; char text[400]; } log[LOG_LINES] = {0};
    long files = 0;
    int scanning = 1;

    // Output is non-blocking: a new frame is only encoded once the terminal has taken the
    // previous one, so a slow terminal drops frames instead of piling up lag, and keys are
    // read while waiting for it.
    out_flags = fcntl(1, F_GETFL);
    fcntl(1, F_SETFL, out_flags | O_NONBLOCK);
    size_t off = 0; // b.p[off..b.n) is still unwritten
    double next = now_s();
    while (!quit) {
        int pending = off < b.n;
        double wait = pending ? 1.0 : next - now_s();
        struct pollfd pfd[2] = {{0, POLLIN, 0}, {1, POLLOUT, 0}};
        poll(pfd, pending ? 2 : 1, wait > 0 ? (int)(wait * 1000) + 1 : 0);
        // terminal gone (closed tab, hangup): quit instead of spinning on a dead fd
        if ((pfd[0].revents | (pending ? pfd[1].revents : 0)) & (POLLHUP | POLLERR | POLLNVAL)) break;
        if (pfd[0].revents & POLLIN) {
            char c;
            ssize_t n = read(0, &c, 1);
            if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) break; // readable but empty: EOF
            for (; n == 1; n = read(0, &c, 1))
                if (c == 'q' || c == 27) quit = 1;
        }
        if (quit) break;
        if (pending) {
            ssize_t k = write(1, b.p + off, b.n - off);
            if (k > 0) off += k;
            else if (k < 0 && errno != EAGAIN && errno != EINTR) break; // terminal gone
            continue;
        }
        if (now_s() < next) continue;
        next = fmax(next + 1.0 / FPS, now_s()); // behind schedule: skip ahead, don't catch up

        double now = now_s(), dt = now - last;
        last = now;

        pthread_mutex_lock(&mu);
        if (shared.any) g.last = now;
        g.files += shared.files_ev, g.added += shared.added, g.removed += shared.removed;
        shared.files_ev = shared.added = shared.removed = 0, shared.any = 0;
        memcpy(log, shared.log, sizeof log);
        files = shared.files, scanning = shared.scanning;
        pthread_mutex_unlock(&mu);
        sig_step(&g, dt);
        t += dt * TIME_SPEED;

        struct winsize ws;
        int c2 = 80, r2 = 24, full = 0;
        if (!ioctl(1, TIOCGWINSZ, &ws) && ws.ws_col && ws.ws_row) c2 = ws.ws_col, r2 = ws.ws_row;
        if (c2 != cols || r2 != rows) {
            cols = c2, rows = r2, full = 1;
            int frows = rows - 1 - LOG_LINES > 0 ? rows - 1 - LOG_LINES : 1;
            px = realloc(px, (size_t)cols * frows * 2 * 3);
            prev = realloc(prev, (size_t)cols * frows * 6);
        }
        int frows = rows - 1 - LOG_LINES > 0 ? rows - 1 - LOG_LINES : 1;
        double age = now - g.last;
        shader_render(&sh, cols, frows * 2, (float)t, &g, (float)age, px);

        b.n = 0;
        bput(&b, "\x1b[?2026h"); // synchronized output: no tearing where supported
        if (full) bput(&b, "\x1b[0m\x1b[2J");
        draw_field(&b, px, prev, cols, frows, full);
        char head[1024];
        int hn = snprintf(head, sizeof head, " vibematrix  files %.2f  +lines %.2f  -lines %.2f  age %3.0fs  [q]uit  %s  %s%ld files  %s",
                          g.load.x, g.download.x, g.upload.x, fmin(age, 999), name, scanning ? "indexing… " : "", files, root); // path last: may be cut
        bput(&b, "\x1b[1;1H\x1b[0;1;97;40m%.*s\x1b[K", hn < cols ? hn : cols, head);
        for (int i = 0; i < LOG_LINES && frows + 2 + i <= rows; i++) {
            const char *col = log[i].kind == '+' ? "32" : log[i].kind == '-' ? "31" : "33";
            bput(&b, "\x1b[%d;1H\x1b[0;%sm %.*s\x1b[K", frows + 2 + i, col, cols > 1 ? cols - 1 : 0, log[i].text);
        }
        bput(&b, "\x1b[0m\x1b[?2026l");
        off = 0;
    }
    // drop any unsent rest of the frame; the ESC starting the reset makes terminals discard a
    // cut-off sequence. Give a terminal that isn't reading (paused, ctrl-s) 1s, then exit anyway.
    const char *reset = "\x1b[0m\x1b[?25h\x1b[?1049l";
    for (size_t n = strlen(reset), sent = 0; sent < n;) {
        struct pollfd pfd = {1, POLLOUT, 0};
        if (poll(&pfd, 1, 1000) <= 0) break;
        ssize_t k = write(1, reset + sent, n - sent);
        if (k <= 0) break;
        sent += k;
    }
    fcntl(1, F_SETFL, out_flags);
    tcsetattr(0, TCSANOW, &saved_term);
}

// ------------------------------------------------------------ settings

typedef struct {
    const char *shader;
    char shader_from[PATH_MAX + 32]; // where the value came from, for error messages
} Settings;

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && strchr(" \t\r\n", e[-1])) *--e = 0;
    return s;
}

// XDG base dir spec: XDG_CONFIG_HOME only counts if absolute, else ~/.config
static int config_path(char *buf, size_t n) {
    const char *x = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    if (x && x[0] == '/') return snprintf(buf, n, "%s/vibematrix/config", x), 1;
    if (home && *home) return snprintf(buf, n, "%s/.config/vibematrix/config", home), 1;
    return 0;
}

// `key = value` lines, `#` comments; a missing file is fine, a wrong one is an error
static void config_read(FILE *f, const char *path, Settings *s) {
    char line[1024];
    for (int no = 1; fgets(line, sizeof line, f); no++) {
        char *l = trim(line), *eq = strchr(l, '=');
        if (!*l || *l == '#') continue;
        if (!eq) die("%s:%d: expected `key = value`, got `%s`", path, no, l);
        *eq = 0;
        char *key = trim(l), *val = trim(eq + 1);
        if (!strcmp(key, "shader") && *val) {
            s->shader = strdup(val);
            snprintf(s->shader_from, sizeof s->shader_from, "%s:%d", path, no);
        } else {
            die("%s:%d: unknown or empty setting `%s` (known: shader)", path, no, key);
        }
    }
}

// ------------------------------------------------------------ self-test

static void selftest(void) {
    // config: comments, blank lines, spacing; later lines win
    Settings cs = {.shader = "default"};
    char cfg[] = "# vibematrix\n\n  shader =  oil \r\n\tshader=smoke\n";
    FILE *cf = fmemopen(cfg, strlen(cfg), "r");
    config_read(cf, "cfg", &cs);
    fclose(cf);
    assert(!strcmp(cs.shader, "smoke") && !strcmp(cs.shader_from, "cfg:4"));
    char cp[PATH_MAX];
    setenv("XDG_CONFIG_HOME", "/x", 1);
    assert(config_path(cp, sizeof cp) && !strcmp(cp, "/x/vibematrix/config"));
    setenv("XDG_CONFIG_HOME", "relative", 1), setenv("HOME", "/h", 1); // relative: ignored per spec
    assert(config_path(cp, sizeof cp) && !strcmp(cp, "/h/.config/vibematrix/config"));

    // path map survives growth
    char p[64];
    for (int i = 0; i < 10000; i++) snprintf(p, sizeof p, "/x/%d", i), entry(p)->size = i;
    for (int i = 0; i < 10000; i++) snprintf(p, sizeof p, "/x/%d", i), assert(entry(p)->size == i);
    assert(skipped("/a/.git/HEAD") && skipped("/a/node_modules") && !skipped("/a/gitx/b"));

    // line counting: + all lines added, ~ edited/added/removed lines, - all lines removed
    long a, r;
    uint64_t x[] = {1, 2, 2, 5}, y[] = {2, 3, 5, 5, 5};
    line_diff(x, 4, y, 5, &a, &r);
    assert(a == 3 && r == 2); // added 3,5,5  removed 1,2
    char dir[] = "/tmp/vibematrix.XXXXXX", f[PATH_MAX];
    assert(mkdtemp(dir));
    strcpy(root, dir);
    snprintf(f, sizeof f, "%s/a.txt", dir);
    FILE *fp = fopen(f, "w");
    fputs("one\ntwo\nthree\n", fp), fclose(fp);
    on_path(f);
    assert(shared.log[LOG_LINES - 1].kind == '+' && shared.added == 3 && shared.files == 1);
    fp = fopen(f, "w"), fputs("one\nTWO\nthree\nfour\nfive", fp), fclose(fp);
    on_path(f);
    assert(shared.log[LOG_LINES - 1].kind == '~' && shared.added == 6 && shared.removed == 1);
    unlink(f), rmdir(dir);
    on_path(f);
    assert(shared.log[LOG_LINES - 1].kind == '-' && shared.removed == 6 && shared.files == 0 && shared.files_ev == 3);
    on_path(f); // already gone: ignored
    assert(shared.files_ev == 3 && !strcmp(shared.log[LOG_LINES - 1].text, "- a.txt  (+0 -5 lines)"));

    // a sudden huge burst moves uniforms gently: bounded speed AND bounded acceleration,
    // still clearly reacts, and decays back when quiet
    Sig g = {.files = 1000, .added = 1e6, .removed = 10};
    double dt = 1.0 / FPS, pv[3] = {0}, px_[3] = {0}, peak[3] = {0};
    for (int i = 0; i < 900; i++) {
        sig_step(&g, dt);
        double xs[3] = {g.load.x, g.download.x, g.upload.x};
        for (int c = 0; c < 3; c++) {
            double v = (xs[c] - px_[c]) / dt;
            assert(fabs(v) <= MAX_SLEW + 1e-9);                       // no jumps
            assert(i == 0 || fabs(v - pv[c]) <= SPRING_W * SPRING_W * dt + 1e-9); // no jerks
            assert(xs[c] >= -1e-9 && xs[c] <= 1 + 1e-9);               // no overshoot
            pv[c] = v, px_[c] = xs[c], peak[c] = fmax(peak[c], xs[c]);
        }
    }
    assert(peak[0] > .5 && peak[1] > .5 && peak[2] > .1 && peak[1] > peak[2]);
    assert(g.load.x < .05 && g.download.x < .05 && g.upload.x < .05);
    // same motion regardless of frame rate
    Sig s30 = {.added = 500}, s5 = {.added = 500};
    for (int i = 0; i < 30; i++) sig_step(&s30, 1.0 / 30);
    for (int i = 0; i < 5; i++) sig_step(&s5, 1.0 / 5);
    assert(fabs(s30.download.x - s5.download.x) < 1e-3);

    // every cube shader compiles and draws something; time the default at a big terminal size
    if (!gl_init()) { // e.g. brew's test sandbox has no GPU access
        puts("shaders skipped: no OpenGL context here");
        puts("ok");
        return;
    }
    DIR *d = opendir(shader_dir);
    struct dirent *e;
    int ok = 0, total = 0, default_ok = 0;
    unsigned char *px = malloc(200 * 120 * 3);
    Sig u = {.load = {.3, 0}, .download = {.3, 0}, .upload = {.3, 0}};
    while (d && (e = readdir(d))) {
        if (!is_shader(e->d_name)) continue;
        char name[256], err[2048];
        snprintf(name, sizeof name, "%.*s", (int)strlen(e->d_name) - 12, e->d_name + 7);
        Shader s;
        total++;
        if (!shader_load(&s, name, err, sizeof err)) {
            printf("  broken %s: %.*s\n", name, (int)strcspn(err, "\n"), err);
            continue;
        }
        memset(px, 0, 64 * 32 * 3);
        shader_render(&s, 64, 32, 3, &u, 1, px);
        int lit = 0;
        for (int i = 0; i < 64 * 32 * 3; i++) lit |= px[i];
        if (!lit) {
            printf("  broken %s: all black\n", name);
            continue;
        }
        ok++;
        if (!strcmp(name, DEFAULT_SHADER)) {
            default_ok = 1;
            Buf b = {0};
            unsigned char *prev = calloc(200 * 60, 6);
            double t0 = now_s();
            for (int i = 0; i < 30; i++) {
                shader_render(&s, 200, 120, 1 + i * .033f, &u, 1, px);
                b.n = 0;
                draw_field(&b, px, prev, 200, 60, i == 0);
            }
            printf("  %s at 200x60 cells: %.1f ms/frame (render + encode), %zu KB last frame\n", name,
                   (now_s() - t0) * 1000 / 30, b.n / 1024);
        }
    }
    if (d) closedir(d);
    printf("shaders ok: %d / %d\n", ok, total);
    assert(default_ok);
    puts("ok");
}

int main(int argc, char **argv) {
    char exe[PATH_MAX], real[PATH_MAX];
    uint32_t n = sizeof exe;
    if (_NSGetExecutablePath(exe, &n) || !realpath(exe, real)) die("cannot locate executable");
    // next to the binary (dev build), else the installed prefix/share/vibematrix/shader (brew)
    char *bindir = dirname(real);
    snprintf(shader_dir, sizeof shader_dir, "%s/shader", bindir);
    if (access(shader_dir, R_OK)) snprintf(shader_dir, sizeof shader_dir, "%s/../share/vibematrix/shader", bindir);

    // answered before reading the config, so a broken config can't get in the way
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--version")) return puts("vibematrix " VERSION), 0;
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            char cp[PATH_MAX];
            printf("vibematrix %s: LED cube GLSL shaders in the terminal, driven by file changes\n\n"
                   "usage: vibematrix [DIR] [--shader NAME]\n\n"
                   "  DIR             directory to watch, recursively (default: .)\n"
                   "  --shader NAME   shader to play (default: %s)\n"
                   "  --test          run the self-test\n"
                   "  --version       print the version\n"
                   "  -h, --help      print this help\n\n"
                   "keys: q or Esc quits (Ctrl-C too)\n\n"
                   "settings, later wins: default < config file < environment < command line\n"
                   "  config file  %s\n"
                   "               shader = NAME\n"
                   "  environment  VIBEMATRIX_SHADER=NAME\n\n"
                   "shaders (%s):\n",
                   VERSION, DEFAULT_SHADER, config_path(cp, sizeof cp) ? cp : "(no $HOME)", shader_dir);
            list_shaders(stdout);
            return 0;
        }
    }

    // later wins: built-in default < config file < environment < command line
    Settings set = {.shader = DEFAULT_SHADER, .shader_from = "built-in default"};
    char cpath[PATH_MAX];
    FILE *cf = config_path(cpath, sizeof cpath) ? fopen(cpath, "r") : NULL;
    if (cf) config_read(cf, cpath, &set), fclose(cf);
    const char *env = getenv("VIBEMATRIX_SHADER");
    if (env && *env) set.shader = env, snprintf(set.shader_from, sizeof set.shader_from, "VIBEMATRIX_SHADER");

    const char *dir = ".";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--test")) return selftest(), 0;
        else if (!strcmp(argv[i], "--shader") && i + 1 < argc)
            set.shader = argv[++i], snprintf(set.shader_from, sizeof set.shader_from, "--shader");
        else if (argv[i][0] == '-') die("unknown option '%s'; see vibematrix --help", argv[i]);
        else dir = argv[i];
    }
    if (!shader_exists(set.shader)) {
        fprintf(stderr, "unknown shader '%s' (from %s); available:", set.shader, set.shader_from);
        list_shaders(stderr);
        return 1;
    }
    struct stat st;
    if (!realpath(dir, root) || stat(root, &st) || !S_ISDIR(st.st_mode)) die("not a directory: %s", dir);
    run(set.shader);
    return 0;
}
