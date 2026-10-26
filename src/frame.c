/*
 * frame - put a screenshot into a phone-frame PNG.
 *
 *   frame detect  <frame.png> [refW refH]
 *   frame size    <image>                 prints "W H" (PNG or JPEG)
 *   frame compose <frame.png> <in.png|in.jpg> <out.png|out.jpg>
 *                 [--dx N] [--dy N] [--scale F] [--mode cover|stretch] [--rot cw|ccw]
 *                 [--res native|frame] [--quality N] [--bg RRGGBB]
 *
 * Input format is detected from the file contents (PNG or JPEG). The output
 * format follows the output file extension: .png is lossless; .jpg/.jpeg is
 * re-encoded (default quality 100, no chroma subsampling) and the frame's
 * transparent area is flattened onto --bg (default ffffff).
 *
 * The screen area is found automatically: the transparent region that is
 * enclosed by the bezel is flood-filled from the image centre. Its shape is
 * used as the mask, so rounded corners / cut-outs match the frame exactly.
 *
 * Exit codes: 0 ok, 1 usage or I/O error, 3 input already has frame size
 * (skip), 4 screen region not enclosed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <strings.h>
#include "lodepng.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

typedef struct { unsigned w, h; unsigned char *p; } Img;

static int is_jpeg_file(const char *path) {
    FILE *f = fopen(path, "rb");
    unsigned char b[3] = {0, 0, 0};
    size_t n;
    if (!f) return 0;
    n = fread(b, 1, 3, f);
    fclose(f);
    return n == 3 && b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF;
}

static int is_jpeg_name(const char *path) {
    const char *d = strrchr(path, '.');
    return d && (!strcasecmp(d, ".jpg") || !strcasecmp(d, ".jpeg"));
}

static int load(const char *path, Img *im) {
    if (is_jpeg_file(path)) {
        int w, h, n;
        unsigned char *p = stbi_load(path, &w, &h, &n, 4);
        if (!p) { fprintf(stderr, "cannot read %s: %s\n", path, stbi_failure_reason()); return 0; }
        im->p = p; im->w = (unsigned)w; im->h = (unsigned)h;
        return 1;
    }
    unsigned e = lodepng_decode32_file(&im->p, &im->w, &im->h, path);
    if (e) { fprintf(stderr, "cannot read %s: %s\n", path, lodepng_error_text(e)); return 0; }
    return 1;
}

static Img rotate90(const Img *s, int cw) {
    Img d; d.w = s->h; d.h = s->w;
    d.p = malloc((size_t)d.w * d.h * 4);
    for (unsigned y = 0; y < d.h; y++)
        for (unsigned x = 0; x < d.w; x++) {
            unsigned sx, sy;
            if (cw) { sx = y; sy = s->h - 1 - x; }
            else    { sx = s->w - 1 - y; sy = x; }
            memcpy(d.p + ((size_t)y * d.w + x) * 4, s->p + ((size_t)sy * s->w + sx) * 4, 4);
        }
    return d;
}

typedef struct { int x, y, w, h; unsigned char *mask; } Region;

/* Flood-fill the transparent screen area. Returns 0 ok, 4 not enclosed, 1 none. */
static int find_region(const Img *f, Region *r) {
    int W = f->w, H = f->h;
    const unsigned char *p = f->p;
#define ALPHA(x, y) p[((size_t)(y) * W + (x)) * 4 + 3]
    int sx = -1, sy = -1, cx = W / 2, cy = H / 2, maxr = (W > H ? W : H) / 2;
    for (int rad = 0; rad <= maxr && sx < 0; rad++)
        for (int dy = -rad; dy <= rad && sx < 0; dy++) {
            int y = cy + dy;
            if (y < 0 || y >= H) continue;
            int step = (dy == -rad || dy == rad) ? 1 : 2 * rad;
            if (step == 0) step = 1;
            for (int dx = -rad; dx <= rad; dx += step) {
                int x = cx + dx;
                if (x < 0 || x >= W) continue;
                if (ALPHA(x, y) <= 127) { sx = x; sy = y; break; }
            }
        }
    if (sx < 0) return 1;
    unsigned char *mask = calloc((size_t)W * H, 1);
    int *stack = malloc(sizeof(int) * (size_t)W * H);
    size_t sp = 0;
    stack[sp++] = sy * W + sx; mask[sy * W + sx] = 1;
    int minx = W, miny = H, maxx = -1, maxy = -1, touch = 0;
    while (sp) {
        int i = stack[--sp], x = i % W, y = i / W;
        if (x < minx) minx = x;
        if (x > maxx) maxx = x;
        if (y < miny) miny = y;
        if (y > maxy) maxy = y;
        if (x == 0 || y == 0 || x == W - 1 || y == H - 1) touch = 1;
        static const int nx[4] = {1, -1, 0, 0}, ny[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; k++) {
            int X = x + nx[k], Y = y + ny[k];
            if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
            size_t j = (size_t)Y * W + X;
            if (!mask[j] && ALPHA(X, Y) <= 127) { mask[j] = 1; stack[sp++] = (int)j; }
        }
    }
    free(stack);
    if (touch) { free(mask); return 4; }
    r->x = minx; r->y = miny; r->w = maxx - minx + 1; r->h = maxy - miny + 1; r->mask = mask;
    return 0;
}

/* Box-filter sample (acts as bilinear when upscaling). u,v in source pixels. */
static void sample(const Img *s, double u, double v, double fx, double fy, unsigned char *out) {
    if (u < 0.5) u = 0.5;
    if (u > s->w - 0.5) u = s->w - 0.5;
    if (v < 0.5) v = 0.5;
    if (v > s->h - 0.5) v = s->h - 0.5;
    double x0 = u - fx / 2, x1 = u + fx / 2, y0 = v - fy / 2, y1 = v + fy / 2;
    if (x0 < 0) x0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y0 < 0) y0 = 0;
    if (y1 > s->h) y1 = s->h;
    double sum[3] = {0, 0, 0}, tot = 0;
    for (int iy = (int)floor(y0); iy < (int)ceil(y1); iy++) {
        double wy = fmin(iy + 1, y1) - fmax(iy, y0);
        if (wy <= 0) continue;
        for (int ix = (int)floor(x0); ix < (int)ceil(x1); ix++) {
            double wx = fmin(ix + 1, x1) - fmax(ix, x0);
            if (wx <= 0) continue;
            const unsigned char *q = s->p + ((size_t)iy * s->w + ix) * 4;
            double w = wx * wy;
            sum[0] += q[0] * w; sum[1] += q[1] * w; sum[2] += q[2] * w; tot += w;
        }
    }
    for (int c = 0; c < 3; c++) out[c] = (unsigned char)(sum[c] / tot + 0.5);
}

/* Resample an RGBA image (premultiplied-alpha maths). Bilinear when enlarging,
 * area average when shrinking. */
static Img resize_rgba(const Img *s, unsigned nw, unsigned nh) {
    Img d; d.w = nw; d.h = nh;
    d.p = malloc((size_t)nw * nh * 4);
    double sx = (double)s->w / nw, sy = (double)s->h / nh;
    int up = sx <= 1 && sy <= 1;
    for (unsigned y = 0; y < nh; y++)
        for (unsigned x = 0; x < nw; x++) {
            double sc[3] = {0, 0, 0}, sa = 0, tot = 0;
            if (up) {
                double u = (x + 0.5) * sx - 0.5, v = (y + 0.5) * sy - 0.5;
                int x0 = (int)floor(u), y0 = (int)floor(v);
                double fx = u - x0, fy = v - y0;
                for (int j = 0; j < 2; j++)
                    for (int i = 0; i < 2; i++) {
                        int X = x0 + i, Y = y0 + j;
                        if (X < 0) X = 0;
                        if (Y < 0) Y = 0;
                        if (X >= (int)s->w) X = s->w - 1;
                        if (Y >= (int)s->h) Y = s->h - 1;
                        double w = (i ? fx : 1 - fx) * (j ? fy : 1 - fy);
                        const unsigned char *q = s->p + ((size_t)Y * s->w + X) * 4;
                        double a = q[3] / 255.0;
                        sc[0] += q[0] * a * w; sc[1] += q[1] * a * w; sc[2] += q[2] * a * w;
                        sa += a * w; tot += w;
                    }
            } else {
                double x0 = x * sx, x1 = (x + 1) * sx, y0 = y * sy, y1 = (y + 1) * sy;
                if (x1 > s->w) x1 = s->w;
                if (y1 > s->h) y1 = s->h;
                for (int iy = (int)floor(y0); iy < (int)ceil(y1); iy++) {
                    double wy = fmin(iy + 1, y1) - fmax(iy, y0);
                    if (wy <= 0) continue;
                    for (int ix = (int)floor(x0); ix < (int)ceil(x1); ix++) {
                        double wx = fmin(ix + 1, x1) - fmax(ix, x0);
                        if (wx <= 0) continue;
                        const unsigned char *q = s->p + ((size_t)iy * s->w + ix) * 4;
                        double a = q[3] / 255.0, w = wx * wy;
                        sc[0] += q[0] * a * w; sc[1] += q[1] * a * w; sc[2] += q[2] * a * w;
                        sa += a * w; tot += w;
                    }
                }
            }
            unsigned char *o = d.p + ((size_t)y * nw + x) * 4;
            if (sa <= 0 || tot <= 0) { o[0] = o[1] = o[2] = o[3] = 0; continue; }
            for (int c = 0; c < 3; c++) {
                double v = sc[c] / sa + 0.5;
                o[c] = (unsigned char)(v > 255 ? 255 : v);
            }
            o[3] = (unsigned char)(sa / tot * 255 + 0.5);
        }
    return d;
}

static int cmd_detect(int argc, char **argv) {
    if (argc < 3) return 1;
    Img f; if (!load(argv[2], &f)) return 1;
    Region r; int e = find_region(&f, &r);
    if (e == 4) { fprintf(stderr, "screen area is not enclosed by the bezel\n"); return 4; }
    if (e) { fprintf(stderr, "no transparent screen area found\n"); return 1; }
    printf("fw=%u\nfh=%u\nx=%d\ny=%d\nw=%d\nh=%d\naspect=%.5f\n", f.w, f.h, r.x, r.y, r.w, r.h, (double)r.w / r.h);
    if (argc >= 5) {
        double ref = atof(argv[3]) / atof(argv[4]);
        printf("aspect_diff=%.5f\n", fabs((double)r.w / r.h - ref) / ref);
    }
    return 0;
}

static int cmd_size(int argc, char **argv) {
    if (argc < 3) return 1;
    unsigned w, h;
    if (is_jpeg_file(argv[2])) {
        int iw, ih, n;
        if (!stbi_info(argv[2], &iw, &ih, &n)) return 1;
        w = (unsigned)iw; h = (unsigned)ih;
    } else {
        unsigned char *buf = NULL; size_t sz = 0; LodePNGState st;
        if (lodepng_load_file(&buf, &sz, argv[2])) return 1;
        lodepng_state_init(&st);
        unsigned e = lodepng_inspect(&w, &h, &st, buf, sz);
        free(buf); lodepng_state_cleanup(&st);
        if (e) return 1;
    }
    printf("%u %u\n", w, h);
    return 0;
}

static int cmd_compose(int argc, char **argv) {
    if (argc < 5) return 1;
    double dx = 0, dy = 0, scale = 1; int cover = 1, cw = 0, native = 0, quality = 100; unsigned bg = 0xFFFFFF;
    for (int i = 5; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--dx")) dx = atof(argv[i + 1]);
        else if (!strcmp(argv[i], "--dy")) dy = atof(argv[i + 1]);
        else if (!strcmp(argv[i], "--scale")) scale = atof(argv[i + 1]);
        else if (!strcmp(argv[i], "--mode")) cover = strcmp(argv[i + 1], "stretch") != 0;
        else if (!strcmp(argv[i], "--rot")) cw = !strcmp(argv[i + 1], "cw");
        else if (!strcmp(argv[i], "--res")) native = !strcmp(argv[i + 1], "native");
        else if (!strcmp(argv[i], "--quality")) { quality = atoi(argv[i + 1]); if (quality < 1) quality = 1; if (quality > 100) quality = 100; }
        else if (!strcmp(argv[i], "--bg")) bg = (unsigned)strtoul(argv[i + 1], NULL, 16) & 0xFFFFFF;
    }
    Img f, s;
    if (!load(argv[2], &f) || !load(argv[3], &s)) return 1;
    if ((s.w == f.w && s.h == f.h) || (s.w == f.h && s.h == f.w)) {
        fprintf(stderr, "input already has frame size, skipped\n");
        return 3;
    }
    Region r; int e = find_region(&f, &r);
    if (e == 4) { fprintf(stderr, "screen area is not enclosed by the bezel\n"); return 4; }
    if (e) { fprintf(stderr, "no transparent screen area found\n"); return 1; }
    /* orientation mismatch (landscape screenshot, portrait frame or vice versa) */
    if ((s.w > s.h) != (r.w > r.h)) {
        Img g = rotate90(&f, cw);
        free(f.p); free(r.mask); f = g;
        e = find_region(&f, &r);
        if (e) { fprintf(stderr, "screen detection failed after rotation\n"); return e == 4 ? 4 : 1; }
    }
    /* native resolution: scale the frame (not the screenshot) so the screenshot keeps
     * its own pixels 1:1 inside the screen area */
    int pixel_exact = 0;
    if (native) {
        double tw0 = r.w * scale, th0 = r.h * scale;
        double kx0 = s.w / tw0, ky0 = s.h / th0;
        double U = cover ? fmin(kx0, ky0) : fmax(kx0, ky0);
        double ow = f.w * U, oh = f.h * U;
        if (U > 0 && fabs(U - 1) > 0.002 && ow * oh <= 36e6) {
            Img g = resize_rgba(&f, (unsigned)(ow + 0.5), (unsigned)(oh + 0.5));
            free(f.p); free(r.mask); f = g;
            dx *= U; dy *= U;
            e = find_region(&f, &r);
            if (e) { fprintf(stderr, "screen detection failed after scaling\n"); return e == 4 ? 4 : 1; }
        } else if (ow * oh > 36e6) {
            fprintf(stderr, "native size too large, using frame size\n");
        }
        pixel_exact = cover;
    }
    int W = f.w, H = f.h;
    /* underlay mask = screen region grown by 2px, so antialiased edges have no gap */
    unsigned char *tmp = calloc((size_t)W * H, 1), *dil = calloc((size_t)W * H, 1);
    const int R = 2;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            for (int k = -R; k <= R; k++) {
                int X = x + k;
                if (X >= 0 && X < W && r.mask[(size_t)y * W + X]) { tmp[(size_t)y * W + x] = 1; break; }
            }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            for (int k = -R; k <= R; k++) {
                int Y = y + k;
                if (Y >= 0 && Y < H && tmp[(size_t)Y * W + x]) { dil[(size_t)y * W + x] = 1; break; }
            }
    /* target rectangle (frame pixels) after offset and scale */
    double tw = r.w * scale, th = r.h * scale;
    double tx = r.x + r.w / 2.0 + dx - tw / 2, ty = r.y + r.h / 2.0 + dy - th / 2;
    double kx, ky, cx0, cy0;           /* source px per target px, source crop origin */
    if (cover && pixel_exact) {
        /* 1:1 pixels, integer aligned: no resampling blur */
        kx = ky = 1;
        cx0 = floor((s.w - tw) / 2); cy0 = floor((s.h - th) / 2);
        tx = floor(tx + 0.5); ty = floor(ty + 0.5);
    } else if (cover) {
        double k = fmax((double)s.w / tw, 0) , k2 = (double)s.h / th;
        double sc = fmin(k, k2);       /* source px per target px; smaller = zoom to cover */
        kx = ky = sc;
        cx0 = (s.w - tw * sc) / 2; cy0 = (s.h - th * sc) / 2;
    } else {
        kx = s.w / tw; ky = s.h / th; cx0 = cy0 = 0;
    }
    double fx = kx > 1 ? kx : 1, fy = ky > 1 ? ky : 1;
    unsigned char *out = malloc((size_t)W * H * 4);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            size_t i = (size_t)y * W + x;
            const unsigned char *fp = f.p + i * 4;
            unsigned char *op = out + i * 4;
            double fa = fp[3] / 255.0, ua = 0;
            unsigned char uc[3] = {0, 0, 0};
            if (dil[i]) {
                sample(&s, cx0 + (x + 0.5 - tx) * kx, cy0 + (y + 0.5 - ty) * ky, fx, fy, uc);
                ua = 1;
            }
            double oa = fa + ua * (1 - fa);
            if (oa <= 0) { op[0] = op[1] = op[2] = op[3] = 0; continue; }
            for (int c = 0; c < 3; c++)
                op[c] = (unsigned char)((fp[c] * fa + uc[c] * ua * (1 - fa)) / oa + 0.5);
            op[3] = (unsigned char)(oa * 255 + 0.5);
        }
    if (is_jpeg_name(argv[4])) {
        /* JPEG has no alpha: flatten onto the background colour */
        unsigned char *rgb = malloc((size_t)W * H * 3);
        unsigned char b3[3] = {(unsigned char)(bg >> 16), (unsigned char)(bg >> 8), (unsigned char)bg};
        for (size_t i = 0, n = (size_t)W * H; i < n; i++) {
            const unsigned char *op = out + i * 4;
            unsigned a = op[3];
            for (int c = 0; c < 3; c++)
                rgb[i * 3 + c] = a == 255 ? op[c] : (unsigned char)((op[c] * a + b3[c] * (255 - a) + 127) / 255);
        }
        int ok = stbi_write_jpg(argv[4], W, H, 3, rgb, quality);
        free(rgb);
        if (!ok) { fprintf(stderr, "cannot write %s\n", argv[4]); return 1; }
        return 0;
    }
    unsigned er = lodepng_encode32_file(argv[4], out, W, H);
    if (er) { fprintf(stderr, "cannot write %s: %s\n", argv[4], lodepng_error_text(er)); return 1; }
    return 0;
}

int main(int argc, char **argv) {
    int rc = 1;
    if (argc >= 2 && !strcmp(argv[1], "detect")) rc = cmd_detect(argc, argv);
    else if (argc >= 2 && !strcmp(argv[1], "compose")) rc = cmd_compose(argc, argv);
    else if (argc >= 2 && !strcmp(argv[1], "size")) rc = cmd_size(argc, argv);
    if (rc == 1 && argc < 3)
        fprintf(stderr, "usage: frame detect <frame.png> [refW refH]\n"
                        "       frame size <image>\n"
                        "       frame compose <frame.png> <in.png|jpg> <out.png|jpg> [--dx N --dy N --scale F --mode cover|stretch --rot cw|ccw --res native|frame --quality N --bg RRGGBB]\n");
    return rc;
}
