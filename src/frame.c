/*
 * frame - put a screenshot into a phone-frame PNG.
 *
 *   frame detect  <frame.png> [refW refH]
 *   frame size    <image>                 prints "W H" (PNG or JPEG)
 *   frame vprep   <frame.png> <videoW> <videoH> <outdir> [same options as compose]
 *                 writes overlay.png + mask.png and prints the video placement (used for screen recordings)
 *   Perspective (3D) frames are detected automatically; extra options for compose/vprep:
 *                 [--quad x0,y0,x1,y1,x2,y2,x3,y3]   screen corners TL,TR,BR,BL in frame pixels (manual)
 *                 [--orient auto|0|90|180|270]       clockwise rotation of the screenshot / video
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


/* ---------- perspective (3D) frames ---------- */
typedef struct { double x[4], y[4]; } Quad;      /* TL, TR, BR, BL in frame pixels */
typedef struct { double px, py, dx, dy; } Line;  /* point + unit direction */

static int fit_line(const double *X, const double *Y, size_t n, Line *L) {
    if (n < 8) return 0;
    double mx = 0, my = 0;
    for (size_t i = 0; i < n; i++) { mx += X[i]; my += Y[i]; }
    mx /= n; my /= n;
    double sxx = 0, sxy = 0, syy = 0;
    for (size_t i = 0; i < n; i++) {
        double a = X[i] - mx, b = Y[i] - my;
        sxx += a * a; sxy += a * b; syy += b * b;
    }
    double th = 0.5 * atan2(2 * sxy, sxx - syy);
    L->px = mx; L->py = my; L->dx = cos(th); L->dy = sin(th);
    return 1;
}

static int line_isect(const Line *a, const Line *b, double *x, double *y) {
    double det = -a->dx * b->dy + b->dx * a->dy;
    if (fabs(det) < 1e-3) return 0;
    double t = ((b->px - a->px) * (-b->dy) + b->dx * (b->py - a->py)) / det;
    *x = a->px + t * a->dx; *y = a->py + t * a->dy;
    return 1;
}

/* Fit the four straight edges of the screen region and intersect them. Works with rounded
 * corners (only the straight middle part of each edge is used). Returns 1 on success. */
static int fit_quad_old(const Region *r, int W, int H, Quad *q) {
    size_t cap = (size_t)(r->w + r->h) * 8 + 64, n = 0;
    double *X = malloc(cap * sizeof(double)), *Y = malloc(cap * sizeof(double));
    double *SX = malloc(cap * sizeof(double)), *SY = malloc(cap * sizeof(double));
    int ok = 0;
    if (!X || !Y || !SX || !SY) goto done;
    for (int y = r->y; y < r->y + r->h; y++)
        for (int x = r->x; x < r->x + r->w; x++) {
            if (!r->mask[(size_t)y * W + x]) continue;
            int edge = x == 0 || y == 0 || x == W - 1 || y == H - 1 ||
                !r->mask[(size_t)y * W + x - 1] || !r->mask[(size_t)y * W + x + 1] ||
                !r->mask[(size_t)(y - 1) * W + x] || !r->mask[(size_t)(y + 1) * W + x];
            if (edge && n < cap) { X[n] = x + 0.5; Y[n] = y + 0.5; n++; }
        }
    if (n < 40) goto done;
    double ex[4], ey[4], best[4] = {1e18, -1e18, -1e18, 1e18};   /* TL min s, TR max d, BR max s, BL min d */
    for (size_t i = 0; i < n; i++) {
        double sm = X[i] + Y[i], df = X[i] - Y[i];
        if (sm < best[0]) { best[0] = sm; ex[0] = X[i]; ey[0] = Y[i]; }
        if (df > best[1]) { best[1] = df; ex[1] = X[i]; ey[1] = Y[i]; }
        if (sm > best[2]) { best[2] = sm; ex[2] = X[i]; ey[2] = Y[i]; }
        if (df < best[3]) { best[3] = df; ex[3] = X[i]; ey[3] = Y[i]; }
    }
    Line ln[4];
    for (int i = 0; i < 4; i++) {
        double ax = ex[i], ay = ey[i], bx = ex[(i + 1) % 4], by = ey[(i + 1) % 4];
        double Lg = hypot(bx - ax, by - ay);
        if (Lg < 20) goto done;
        Line cur = {ax, ay, (bx - ax) / Lg, (by - ay) / Lg};
        double tol = 0.06 * Lg + 10;
        for (int it = 0; it < 8; it++) {
            double ux = cur.dx, uy = cur.dy, nx = -uy, ny = ux;
            double ta = (ax - cur.px) * ux + (ay - cur.py) * uy, tb = (bx - cur.px) * ux + (by - cur.py) * uy;
            double lo = ta + 0.15 * (tb - ta), hi = ta + 0.85 * (tb - ta);
            if (lo > hi) { double t = lo; lo = hi; hi = t; }
            size_t m = 0;
            for (size_t k = 0; k < n; k++) {
                double dxp = X[k] - cur.px, dyp = Y[k] - cur.py;
                double t = dxp * ux + dyp * uy, d = dxp * nx + dyp * ny;
                if (t >= lo && t <= hi && fabs(d) <= tol) { SX[m] = X[k]; SY[m] = Y[k]; m++; }
            }
            if (!fit_line(SX, SY, m, &cur)) goto done;
            tol = fmax(2.0, tol * 0.5);
        }
        ln[i] = cur;
    }
    for (int i = 0; i < 4; i++)
        if (!line_isect(&ln[(i + 3) % 4], &ln[i], &q->x[i], &q->y[i])) goto done;
    /* convex and sane */
    double sgn = 0;
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4, k = (i + 2) % 4;
        double cr = (q->x[j] - q->x[i]) * (q->y[k] - q->y[j]) - (q->y[j] - q->y[i]) * (q->x[k] - q->x[j]);
        if (cr == 0 || (sgn != 0 && (cr > 0) != (sgn > 0))) goto done;
        sgn = cr;
    }
    for (int i = 0; i < 4; i++)
        if (q->x[i] < -W * 0.5 || q->x[i] > W * 1.5 || q->y[i] < -H * 0.5 || q->y[i] > H * 1.5) goto done;
    ok = 1;
done:
    free(X); free(Y); free(SX); free(SY);
    return ok;
}

/* Primary detector. The region is convex-ish; for every direction t (0.5 deg steps) the "support
 * line" touches the outline, and the number of outline pixels lying within eps of that line is the
 * length of the straight edge facing t. Four real edges give four sharp peaks; rounded corners
 * (however large) only give small counts. Each peak is then refined with a total-least-squares
 * fit and the four lines are intersected. No ordering of the outline is needed. */
static int fit_quad_new(const Region *r, int W, int H, Quad *q) {
    enum { NA = 720 };
    size_t cap = (size_t)(r->w + r->h) * 8 + 64, n = 0;
    double *X = malloc(cap * sizeof(double)), *Y = malloc(cap * sizeof(double));
    double *SX = malloc(cap * sizeof(double)), *SY = malloc(cap * sizeof(double));
    double h[NA], cnt[NA];
    int ok = 0;
    if (!X || !Y || !SX || !SY) goto done;
    for (int y = r->y; y < r->y + r->h; y++)
        for (int x = r->x; x < r->x + r->w; x++) {
            if (!r->mask[(size_t)y * W + x]) continue;
            int edge = x == 0 || y == 0 || x == W - 1 || y == H - 1 ||
                !r->mask[(size_t)y * W + x - 1] || !r->mask[(size_t)y * W + x + 1] ||
                !r->mask[(size_t)(y - 1) * W + x] || !r->mask[(size_t)(y + 1) * W + x];
            if (edge && n < cap) { X[n] = x + 0.5; Y[n] = y + 0.5; n++; }
        }
    if (n < 40) goto done;
    double eps = fmax(1.5, 0.0012 * fmax(r->w, r->h));
    for (int a = 0; a < NA; a++) {
        double th = a * M_PI / (NA / 2), c = cos(th), s = sin(th), mx = -1e30;
        for (size_t i = 0; i < n; i++) { double v = X[i] * c + Y[i] * s; if (v > mx) mx = v; }
        h[a] = mx;
        size_t k = 0;
        for (size_t i = 0; i < n; i++) if (X[i] * c + Y[i] * s >= mx - eps) k++;
        cnt[a] = (double)k;
    }
    /* light smoothing, then pick four peaks at least 25 degrees apart */
    double sm[NA];
    for (int a = 0; a < NA; a++) sm[a] = (cnt[(a + NA - 1) % NA] + 2 * cnt[a] + cnt[(a + 1) % NA]) / 4;
    int pk[4];
    for (int k = 0; k < 4; k++) {
        int bi = -1; double bv = 0;
        for (int a = 0; a < NA; a++) if (sm[a] > bv) { bv = sm[a]; bi = a; }
        if (bi < 0 || bv < 8) goto done;
        pk[k] = bi;
        for (int d = -50; d <= 50; d++) sm[(bi + d + NA) % NA] = 0;
    }
    /* sort by angle */
    for (int i = 0; i < 4; i++) for (int j = i + 1; j < 4; j++) if (pk[j] < pk[i]) { int t = pk[i]; pk[i] = pk[j]; pk[j] = t; }
    /* Lines: normal N=(cos t, sin t), points p with p.N = off. Refine each by TLS on its support pixels. */
    double nxv[4], nyv[4], off[4];
    for (int k = 0; k < 4; k++) {
        double th = pk[k] * M_PI / (NA / 2), c = cos(th), s = sin(th), o = h[pk[k]];
        double tol = eps;
        for (int it = 0; it < 6; it++) {
            size_t m = 0;
            for (size_t i = 0; i < n; i++) {
                double d = X[i] * c + Y[i] * s - o;
                if (d >= -tol && d <= 0.75) { SX[m] = X[i]; SY[m] = Y[i]; m++; }
            }
            Line L;
            if (!fit_line(SX, SY, m, &L)) goto done;
            double nx = -L.dy, ny = L.dx;
            if (nx * c + ny * s < 0) { nx = -nx; ny = -ny; }
            c = nx; s = ny; o = L.px * nx + L.py * ny;
            tol = fmax(1.0, tol * 0.7);
        }
        /* the fitted line may sit inside the outline by sub-pixel noise: push out so that the outline stays inside */
        double mx = -1e30;
        for (size_t i = 0; i < n; i++) { double v = X[i] * c + Y[i] * s; if (v > mx) mx = v; }
        nxv[k] = c; nyv[k] = s; off[k] = fmax(o, mx - 1.0);
    }
    for (int i = 0; i < 4; i++) {            /* corner i = intersection of edge i and edge i+1 (consecutive normals) */
        int j = (i + 1) % 4;
        double det = nxv[i] * nyv[j] - nyv[i] * nxv[j];
        if (fabs(det) < 0.05) goto done;
        q->x[i] = (off[i] * nyv[j] - nyv[i] * off[j]) / det;
        q->y[i] = (nxv[i] * off[j] - off[i] * nxv[j]) / det;
    }
    /* Label the corners TL, TR, BR, BL going clockwise on screen: TL is the corner where the edge to the
     * next corner is the most horizontal (smallest |angle|), the same way an upright rectangle is labelled. */
    {
        Quad t = *q;
        double area = 0;
        for (int i = 0; i < 4; i++) { int j = (i + 1) % 4; area += t.x[i] * t.y[j] - t.x[j] * t.y[i]; }
        if (area < 0) {                       /* counter-clockwise: reverse */
            for (int i = 0; i < 4; i++) { t.x[i] = q->x[3 - i]; t.y[i] = q->y[3 - i]; }
        }
        int first = 0; double best = 1e30;
        for (int i = 0; i < 4; i++) {
            int j = (i + 1) % 4;
            double ang = fabs(atan2(t.y[j] - t.y[i], t.x[j] - t.x[i]));
            if (ang < best - 1e-9) { best = ang; first = i; }
        }
        for (int i = 0; i < 4; i++) { q->x[i] = t.x[(first + i) % 4]; q->y[i] = t.y[(first + i) % 4]; }
    }
    /* convex and sane, and the whole outline must lie inside the quad */
    double sgn = 0;
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4, k = (i + 2) % 4;
        double cr = (q->x[j] - q->x[i]) * (q->y[k] - q->y[j]) - (q->y[j] - q->y[i]) * (q->x[k] - q->x[j]);
        if (cr == 0 || (sgn != 0 && (cr > 0) != (sgn > 0))) goto done;
        sgn = cr;
    }
    for (int i = 0; i < 4; i++)
        if (q->x[i] < -W * 0.5 || q->x[i] > W * 1.5 || q->y[i] < -H * 0.5 || q->y[i] > H * 1.5) goto done;
    {
        double slack = 2.0 + 0.002 * fmax(r->w, r->h);
        for (size_t p = 0; p < n; p += 3) {
            for (int i = 0; i < 4; i++) {
                int j = (i + 1) % 4;
                double ex = q->x[j] - q->x[i], ey = q->y[j] - q->y[i], L = hypot(ex, ey);
                if (L < 1) goto done;
                double d = ((X[p] - q->x[i]) * ey - (Y[p] - q->y[i]) * ex) / L;   /* >0: outside when the quad runs clockwise on screen */
                if ((sgn > 0 ? d : -d) > slack) goto done;
            }
        }
    }
    ok = 1;
done:
    free(X); free(Y); free(SX); free(SY);
    return ok;
}

static int fit_quad(const Region *r, int W, int H, Quad *q) {
    if (fit_quad_new(r, W, H, q)) return 1;
    return fit_quad_old(r, W, H, q);
}

/* the quad is (almost) the region's bounding rectangle: use the flat path */
static int quad_is_flat(const Quad *q, const Region *r) {
    double tol = 2.5 + 0.0015 * fmax(r->w, r->h);
    double X0 = r->x, X1 = r->x + r->w, Y0 = r->y, Y1 = r->y + r->h;
    double cx[4] = {X0, X1, X1, X0}, cy[4] = {Y0, Y0, Y1, Y1};
    for (int i = 0; i < 4; i++)
        if (fabs(q->x[i] - cx[i]) > tol || fabs(q->y[i] - cy[i]) > tol) return 0;
    return 1;
}

/* translate, scale about the centre, then grow slightly so antialiased edges leave no gap */
static void quad_xform(Quad *q, double dx, double dy, double scale, double grow) {
    double cx = 0, cy = 0;
    for (int i = 0; i < 4; i++) { cx += q->x[i]; cy += q->y[i]; }
    cx /= 4; cy /= 4;
    double w = (hypot(q->x[1] - q->x[0], q->y[1] - q->y[0]) + hypot(q->x[2] - q->x[3], q->y[2] - q->y[3])) / 2;
    double h = (hypot(q->x[3] - q->x[0], q->y[3] - q->y[0]) + hypot(q->x[2] - q->x[1], q->y[2] - q->y[1])) / 2;
    double k = scale * (1 + grow / (0.5 * fmin(w, h)));
    for (int i = 0; i < 4; i++) {
        q->x[i] = cx + (q->x[i] - cx) * k + dx;
        q->y[i] = cy + (q->y[i] - cy) * k + dy;
    }
}

static int quad_landscape(const Quad *q) {
    double w = (hypot(q->x[1] - q->x[0], q->y[1] - q->y[0]) + hypot(q->x[2] - q->x[3], q->y[2] - q->y[3])) / 2;
    double h = (hypot(q->x[3] - q->x[0], q->y[3] - q->y[0]) + hypot(q->x[2] - q->x[1], q->y[2] - q->y[1])) / 2;
    return w > h;
}

/* homography mapping the rectangle (0,0)-(w,h) onto the quad: H (row-major 3x3) */
static int homography(double w, double h, const Quad *q, double Hm[9]) {
    double sx[4] = {0, w, w, 0}, sy[4] = {0, 0, h, h};
    double A[8][9];
    for (int i = 0; i < 4; i++) {
        double X = sx[i], Y = sy[i], u = q->x[i], v = q->y[i];
        double r1[9] = {X, Y, 1, 0, 0, 0, -u * X, -u * Y, u};
        double r2[9] = {0, 0, 0, X, Y, 1, -v * X, -v * Y, v};
        memcpy(A[2 * i], r1, sizeof r1); memcpy(A[2 * i + 1], r2, sizeof r2);
    }
    for (int c = 0; c < 8; c++) {
        int p = c;
        for (int r = c + 1; r < 8; r++) if (fabs(A[r][c]) > fabs(A[p][c])) p = r;
        if (fabs(A[p][c]) < 1e-12) return 0;
        if (p != c) for (int k = 0; k < 9; k++) { double t = A[c][k]; A[c][k] = A[p][k]; A[p][k] = t; }
        for (int r = 0; r < 8; r++) {
            if (r == c) continue;
            double f = A[r][c] / A[c][c];
            for (int k = c; k < 9; k++) A[r][k] -= f * A[c][k];
        }
    }
    for (int i = 0; i < 8; i++) Hm[i] = A[i][8] / A[i][i];
    Hm[8] = 1;
    return 1;
}

static int invert3(const double m[9], double o[9]) {
    double a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], i = m[8];
    double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (fabs(det) < 1e-18) return 0;
    o[0] = (e * i - f * h) / det; o[1] = (c * h - b * i) / det; o[2] = (b * f - c * e) / det;
    o[3] = (f * g - d * i) / det; o[4] = (a * i - c * g) / det; o[5] = (c * d - a * f) / det;
    o[6] = (d * h - e * g) / det; o[7] = (b * g - a * h) / det; o[8] = (a * e - b * d) / det;
    return 1;
}


/* Which edge of the quad is the phone's top? The front camera / notch / dynamic island is an opaque
 * cut-out inside (or at the border of) the transparent screen area, close to the top edge.
 * Edge i runs from corner i to corner i+1 (TL->TR, TR->BR, BR->BL, BL->TL). Returns 0..3, or -1
 * when no clear cut-out is found. */
static int detect_top(const Region *r, int W, int H, const Quad *q) {
    double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
    double len[4], ux[4], uy[4];
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        if (q->x[i] < minx) minx = q->x[i];
        if (q->x[i] > maxx) maxx = q->x[i];
        if (q->y[i] < miny) miny = q->y[i];
        if (q->y[i] > maxy) maxy = q->y[i];
        len[i] = hypot(q->x[j] - q->x[i], q->y[j] - q->y[i]);
        if (len[i] < 10) return -1;
        ux[i] = (q->x[j] - q->x[i]) / len[i]; uy[i] = (q->y[j] - q->y[i]) / len[i];
    }
    double depth[4], cnt[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; i++) depth[i] = 0.07 * (len[(i + 1) % 4] + len[(i + 3) % 4]) / 2;
    int x0 = (int)floor(minx), x1 = (int)ceil(maxx), y0 = (int)floor(miny), y1 = (int)ceil(maxy);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W - 1) x1 = W - 1;
    if (y1 > H - 1) y1 = H - 1;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            if (r->mask[(size_t)y * W + x]) continue;           /* transparent: nothing to count */
            double px = x + 0.5, py = y + 0.5;
            int inside = 1;
            for (int i = 0; i < 4 && inside; i++) {
                int j = (i + 1) % 4;
                double cr = (q->x[j] - q->x[i]) * (py - q->y[i]) - (q->y[j] - q->y[i]) * (px - q->x[i]);
                if (cr < 0) inside = 0;
            }
            if (!inside) continue;
            for (int i = 0; i < 4; i++) {
                double ax = px - q->x[i], ay = py - q->y[i];
                double sp = (ax * ux[i] + ay * uy[i]) / len[i], d = ax * (-uy[i]) + ay * ux[i];
                if (sp > 0.25 && sp < 0.75 && d > 0 && d < depth[i]) cnt[i] += 1;
            }
        }
    double frac[4]; int best = 0;
    for (int i = 0; i < 4; i++) {
        frac[i] = cnt[i] / (0.5 * len[i] * depth[i]);
        if (frac[i] > frac[best]) best = i;
    }
    double second = 0;
    for (int i = 0; i < 4; i++) if (i != best && frac[i] > second) second = frac[i];
    if (frac[best] >= 0.002 && frac[best] >= 3 * second) return best;
    return -1;
}

/* quarter turns (clockwise) to apply to the screenshot/video so its top edge lands on the phone's top */
static int auto_orient(int top, int content_landscape, int quad_land, int cw) {
    if (top >= 0) return (top + (content_landscape ? (cw ? 3 : 1) : 0)) & 3;
    return (content_landscape != quad_land) ? (cw ? 1 : 3) : 0;
}

/* screen region grown by 2px, so antialiased edges leave no gap; 255 = inside */
static unsigned char *make_dil(const Region *r, int W, int H) {
    unsigned char *tmp = calloc((size_t)W * H, 1), *dil = calloc((size_t)W * H, 1);
    const int R = 2;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            for (int k = -R; k <= R; k++) {
                int X = x + k;
                if (X >= 0 && X < W && r->mask[(size_t)y * W + X]) { tmp[(size_t)y * W + x] = 1; break; }
            }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            for (int k = -R; k <= R; k++) {
                int Y = y + k;
                if (Y >= 0 && Y < H && tmp[(size_t)Y * W + x]) { dil[(size_t)y * W + x] = 255; break; }
            }
    free(tmp);
    return dil;
}

static int parse_quad(const char *s, Quad *q) {
    double v[8];
    if (sscanf(s, "%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) != 8) return 0;
    for (int i = 0; i < 4; i++) { q->x[i] = v[2 * i]; q->y[i] = v[2 * i + 1]; }
    return 1;
}

static int parse_orient(const char *s) {       /* -1 auto, else clockwise quarter turns */
    if (!strcmp(s, "90")) return 1;
    if (!strcmp(s, "180")) return 2;
    if (!strcmp(s, "270")) return 3;
    if (!strcmp(s, "0")) return 0;
    return -1;
}

static int write_result(const char *path, unsigned char *out, int W, int H, unsigned bg, int quality) {
    if (is_jpeg_name(path)) {
        /* JPEG has no alpha: flatten onto the background colour */
        unsigned char *rgb = malloc((size_t)W * H * 3);
        unsigned char b3[3] = {(unsigned char)(bg >> 16), (unsigned char)(bg >> 8), (unsigned char)bg};
        for (size_t i = 0, n = (size_t)W * H; i < n; i++) {
            const unsigned char *op = out + i * 4;
            unsigned a = op[3];
            for (int c = 0; c < 3; c++)
                rgb[i * 3 + c] = a == 255 ? op[c] : (unsigned char)((op[c] * a + b3[c] * (255 - a) + 127) / 255);
        }
        int ok = stbi_write_jpg(path, W, H, 3, rgb, quality);
        free(rgb);
        if (!ok) { fprintf(stderr, "cannot write %s\n", path); return 1; }
        return 0;
    }
    unsigned er = lodepng_encode32_file(path, out, W, H);
    if (er) { fprintf(stderr, "cannot write %s: %s\n", path, lodepng_error_text(er)); return 1; }
    return 0;
}

/* screenshot warped into the quad (perspective frames) */
static int compose_persp(const char *outpath, const Img *f, const Region *r, const Img *s0, Quad q,
                         double dx, double dy, double scale, int orient, int cw, unsigned bg, int quality) {
    int k = orient;
    if (k < 0) k = auto_orient(detect_top(r, f->w, f->h, &q), s0->w > s0->h, quad_landscape(&q), 0);   /* 3D frames: the rot setting does not apply, landscape content / unknown top turn counter-clockwise; use orient for anything else */
    Img s = *s0; int owned = 0;
    for (int i = 0; i < k; i++) { Img g = rotate90(&s, 1); if (owned) free(s.p); s = g; owned = 1; }
    quad_xform(&q, dx, dy, scale, 1.5);
    double Hm[9], G[9];
    if (!homography(s.w, s.h, &q, Hm) || !invert3(Hm, G)) { fprintf(stderr, "bad screen corners\n"); return 1; }
    int W = f->w, H = f->h;
    unsigned char *dil = make_dil(r, W, H);
    unsigned char *out = malloc((size_t)W * H * 4);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            size_t i = (size_t)y * W + x;
            const unsigned char *fp = f->p + i * 4;
            unsigned char *op = out + i * 4;
            double fa = fp[3] / 255.0, ua = 0;
            unsigned char uc[3] = {0, 0, 0};
            if (dil[i]) {
                double X = x + 0.5, Y = y + 0.5;
                double nu = G[0] * X + G[1] * Y + G[2], nv = G[3] * X + G[4] * Y + G[5], wq = G[6] * X + G[7] * Y + G[8];
                double u = nu / wq, v = nv / wq, w2 = wq * wq;
                double dudx = (G[0] * wq - nu * G[6]) / w2, dudy = (G[1] * wq - nu * G[7]) / w2;
                double dvdx = (G[3] * wq - nv * G[6]) / w2, dvdy = (G[4] * wq - nv * G[7]) / w2;
                double fx = fabs(dudx) + fabs(dudy), fy = fabs(dvdx) + fabs(dvdy);
                double gx = fx > 1 ? fx : 1, gy = fy > 1 ? fy : 1;
                if (u < -2 * gx || u > s.w + 2 * gx || v < -2 * gy || v > s.h + 2 * gy) { uc[0] = uc[1] = uc[2] = 0; }
                else sample(&s, u, v, gx, gy, uc);
                ua = 1;
            }
            double oa = fa + ua * (1 - fa);
            if (oa <= 0) { op[0] = op[1] = op[2] = op[3] = 0; continue; }
            for (int c = 0; c < 3; c++)
                op[c] = (unsigned char)((fp[c] * fa + uc[c] * ua * (1 - fa)) / oa + 0.5);
            op[3] = (unsigned char)(oa * 255 + 0.5);
        }
    free(dil);
    return write_result(outpath, out, W, H, bg, quality);
}

static int cmd_detect(int argc, char **argv) {
    if (argc < 3) return 1;
    Img f; if (!load(argv[2], &f)) return 1;
    Region r; int e = find_region(&f, &r);
    if (e == 4) { fprintf(stderr, "screen area is not enclosed by the bezel\n"); return 4; }
    if (e) { fprintf(stderr, "no transparent screen area found\n"); return 1; }
    printf("fw=%u\nfh=%u\nx=%d\ny=%d\nw=%d\nh=%d\naspect=%.5f\n", f.w, f.h, r.x, r.y, r.w, r.h, (double)r.w / r.h);
    Quad q;
    if (fit_quad(&r, f.w, f.h, &q)) {
        printf("persp=%d\nquad=%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f\ntop=%d\n", !quad_is_flat(&q, &r),
               q.x[0], q.y[0], q.x[1], q.y[1], q.x[2], q.y[2], q.x[3], q.y[3], detect_top(&r, f.w, f.h, &q));
    } else printf("persp=0\n");
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
    double dx = 0, dy = 0, scale = 1; int cover = 1, cw = 0, native = 0, quality = 100, orient = -1, have_quad = 0; unsigned bg = 0xFFFFFF;
    Quad mq;
    for (int i = 5; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--quad")) have_quad = parse_quad(argv[i + 1], &mq);
        else if (!strcmp(argv[i], "--orient")) orient = parse_orient(argv[i + 1]);
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
    {   /* tilted / perspective screen: warp the screenshot into the four corners */
        Quad q; int persp = 0;
        if (have_quad) { q = mq; persp = 1; }
        else if (fit_quad(&r, f.w, f.h, &q) && !quad_is_flat(&q, &r)) persp = 1;
        if (persp) return compose_persp(argv[4], &f, &r, &s, q, dx, dy, scale, orient, cw, bg, quality);
    }
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
                if (scale < 0.999 && (x + 0.5 < tx - 1 || x + 0.5 > tx + tw + 1 || y + 0.5 < ty - 1 || y + 0.5 > ty + th + 1)) { uc[0] = uc[1] = uc[2] = 0; }
                else sample(&s, cx0 + (x + 0.5 - tx) * kx, cy0 + (y + 0.5 - ty) * ky, fx, fy, uc);
                ua = 1;
            }
            double oa = fa + ua * (1 - fa);
            if (oa <= 0) { op[0] = op[1] = op[2] = op[3] = 0; continue; }
            for (int c = 0; c < 3; c++)
                op[c] = (unsigned char)((fp[c] * fa + uc[c] * ua * (1 - fa)) / oa + 0.5);
            op[3] = (unsigned char)(oa * 255 + 0.5);
        }
    return write_result(argv[4], out, W, H, bg, quality);
}


/* vprep: layout for framing a video. Writes <outdir>/overlay.png (frame at output size)
 * and <outdir>/mask.png (screen area, grey; white = video visible) and prints the placement.
 * The mask follows the frame's real screen shape, so the video never spills past rounded
 * corners / cut-outs. Output size is made even (video encoders need that). */

/* vprep for perspective frames: overlay + mask at output size, and where/how to warp the video */
static int vprep_persp(const char *outdir, const Img *f, const Region *r, Quad q, unsigned vw, unsigned vh,
                       double dx, double dy, double scale, int orient, int cw) {
    int k = orient;
    if (k < 0) k = auto_orient(detect_top(r, f->w, f->h, &q), vw > vh, quad_landscape(&q), 0);   /* same rule as compose_persp */
    const char *rot = k == 1 ? "transpose=1" : k == 2 ? "hflip,vflip" : k == 3 ? "transpose=2" : "";
    quad_xform(&q, dx, dy, scale, 1.5);
    int W = f->w & ~1, H = f->h & ~1, FW = f->w;
    unsigned char *dil = make_dil(r, FW, f->h);
    unsigned char *ov = malloc((size_t)W * H * 4), *mk3 = malloc((size_t)W * H * 3);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            memcpy(ov + ((size_t)y * W + x) * 4, f->p + ((size_t)y * FW + x) * 4, 4);
            unsigned char m = dil[(size_t)y * FW + x];
            mk3[((size_t)y * W + x) * 3] = mk3[((size_t)y * W + x) * 3 + 1] = mk3[((size_t)y * W + x) * 3 + 2] = m;
        }
    char path[4096]; unsigned er;
    snprintf(path, sizeof path, "%s/overlay.png", outdir);
    if ((er = lodepng_encode32_file(path, ov, W, H))) { fprintf(stderr, "cannot write %s: %s\n", path, lodepng_error_text(er)); return 1; }
    snprintf(path, sizeof path, "%s/mask.png", outdir);
    if ((er = lodepng_encode24_file(path, mk3, W, H))) { fprintf(stderr, "cannot write %s: %s\n", path, lodepng_error_text(er)); return 1; }
    double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
    for (int i = 0; i < 4; i++) {
        if (q.x[i] < minx) minx = q.x[i];
        if (q.x[i] > maxx) maxx = q.x[i];
        if (q.y[i] < miny) miny = q.y[i];
        if (q.y[i] > maxy) maxy = q.y[i];
    }
    int bx = (int)floor(minx) - 3, by = (int)floor(miny) - 3, ex = (int)ceil(maxx) + 3, ey = (int)ceil(maxy) + 3;
    if (bx < 0) bx = 0;
    if (by < 0) by = 0;
    if (ex > W) ex = W;
    if (ey > H) ey = H;
    int bw = (ex - bx) & ~1, bh = (ey - by) & ~1;
    if (bw < 8 || bh < 8) { fprintf(stderr, "bad screen corners\n"); return 1; }
    printf("persp=1\nout_w=%d\nout_h=%d\nvf_rot=%s\nbx=%d\nby=%d\nbw=%d\nbh=%d\n", W, H, rot, bx, by, bw, bh);
    printf("p0x=%.2f\np0y=%.2f\np1x=%.2f\np1y=%.2f\np2x=%.2f\np2y=%.2f\np3x=%.2f\np3y=%.2f\n",
           q.x[0] - bx, q.y[0] - by, q.x[1] - bx, q.y[1] - by, q.x[3] - bx, q.y[3] - by, q.x[2] - bx, q.y[2] - by);
    return 0;
}

static int cmd_vprep(int argc, char **argv) {
    if (argc < 6) return 1;
    double dx = 0, dy = 0, scale = 1; int cover = 1, cw = 0, native = 0, orient = -1, have_quad = 0;
    Quad mq;
    unsigned vw = (unsigned)atoi(argv[3]), vh = (unsigned)atoi(argv[4]);
    const char *outdir = argv[5];
    for (int i = 6; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--quad")) have_quad = parse_quad(argv[i + 1], &mq);
        else if (!strcmp(argv[i], "--orient")) orient = parse_orient(argv[i + 1]);
        if (!strcmp(argv[i], "--dx")) dx = atof(argv[i + 1]);
        else if (!strcmp(argv[i], "--dy")) dy = atof(argv[i + 1]);
        else if (!strcmp(argv[i], "--scale")) scale = atof(argv[i + 1]);
        else if (!strcmp(argv[i], "--mode")) cover = strcmp(argv[i + 1], "stretch") != 0;
        else if (!strcmp(argv[i], "--rot")) cw = !strcmp(argv[i + 1], "cw");
        else if (!strcmp(argv[i], "--res")) native = !strcmp(argv[i + 1], "native");
    }
    if (!vw || !vh) return 1;
    Img f;
    if (!load(argv[2], &f)) return 1;
    Region r; int e = find_region(&f, &r);
    if (e == 4) { fprintf(stderr, "screen area is not enclosed by the bezel\n"); return 4; }
    if (e) { fprintf(stderr, "no transparent screen area found\n"); return 1; }
    {
        Quad q; int persp = 0;
        if (have_quad) { q = mq; persp = 1; }
        else if (fit_quad(&r, f.w, f.h, &q) && !quad_is_flat(&q, &r)) persp = 1;
        if (persp) return vprep_persp(outdir, &f, &r, q, vw, vh, dx, dy, scale, orient, cw);
    }
    printf("persp=0\n");
    if ((vw > vh) != (r.w > r.h)) {
        Img g = rotate90(&f, cw);
        free(f.p); free(r.mask); f = g;
        e = find_region(&f, &r);
        if (e) { fprintf(stderr, "screen detection failed after rotation\n"); return e == 4 ? 4 : 1; }
    }
    int exact = 0;
    if (native) {
        double kx0 = vw / (r.w * scale), ky0 = vh / (r.h * scale);
        double U = cover ? fmin(kx0, ky0) : fmax(kx0, ky0);
        double ow = f.w * U, oh = f.h * U;
        if (U > 0 && fabs(U - 1) > 0.002 && ow * oh <= 36e6) {
            Img g = resize_rgba(&f, (unsigned)(ow + 0.5), (unsigned)(oh + 0.5));
            free(f.p); free(r.mask); f = g;
            dx *= U; dy *= U;
            e = find_region(&f, &r);
            if (e) { fprintf(stderr, "screen detection failed after scaling\n"); return e == 4 ? 4 : 1; }
        }
        exact = cover;
    }
    int W = f.w & ~1, H = f.h & ~1, FW = f.w;
    unsigned char *tmp = calloc((size_t)FW * f.h, 1), *dil = calloc((size_t)FW * f.h, 1);
    const int R = 2;
    for (int y = 0; y < (int)f.h; y++)
        for (int x = 0; x < FW; x++)
            for (int k = -R; k <= R; k++) {
                int X = x + k;
                if (X >= 0 && X < FW && r.mask[(size_t)y * FW + X]) { tmp[(size_t)y * FW + x] = 1; break; }
            }
    for (int y = 0; y < (int)f.h; y++)
        for (int x = 0; x < FW; x++)
            for (int k = -R; k <= R; k++) {
                int Y = y + k;
                if (Y >= 0 && Y < (int)f.h && tmp[(size_t)Y * FW + x]) { dil[(size_t)y * FW + x] = 255; break; }
            }
    double tw = r.w * scale, th = r.h * scale;
    double tx = r.x + r.w / 2.0 + dx - tw / 2, ty = r.y + r.h / 2.0 + dy - th / 2;
    double kx, ky, cx0, cy0;
    if (cover && exact) {
        kx = ky = 1;
        cx0 = floor((vw - tw) / 2); cy0 = floor((vh - th) / 2);
        tx = floor(tx + 0.5); ty = floor(ty + 0.5);
    } else if (cover) {
        double sc = fmin((double)vw / tw, (double)vh / th);
        kx = ky = sc;
        cx0 = (vw - tw * sc) / 2; cy0 = (vh - th * sc) / 2;
    } else {
        kx = vw / tw; ky = vh / th; cx0 = cy0 = 0;
    }
    /* video as it will be placed: scaled size, crop origin and size (target pixels) */
    int sw = (int)ceil(vw / kx - 1e-6), sh = (int)ceil(vh / ky - 1e-6);
    sw = (sw + 1) & ~1; sh = (sh + 1) & ~1;          /* even first, so the crop below always fits */
    int cx = (int)floor(cx0 / kx + 0.5), cy = (int)floor(cy0 / ky + 0.5);
    int cw2 = (int)ceil(tw - 1e-6), ch2 = (int)ceil(th - 1e-6);
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    if (cw2 > sw - cx) cw2 = sw - cx;
    if (ch2 > sh - cy) ch2 = sh - cy;
    int px = (int)floor(tx + 0.5), py = (int)floor(ty + 0.5);
    /* keep the placed rectangle inside the canvas */
    if (px < 0) { cx += -px; cw2 -= -px; px = 0; }
    if (py < 0) { cy += -py; ch2 -= -py; py = 0; }
    if (px + cw2 > W) cw2 = W - px;
    if (py + ch2 > H) ch2 = H - py;
    if (cx + cw2 > sw) cw2 = sw - cx;                /* the crop must never be larger than the scaled video */
    if (cy + ch2 > sh) ch2 = sh - cy;
    if (cw2 < 2 || ch2 < 2) { fprintf(stderr, "bad placement\n"); return 1; }
    unsigned char *ov = malloc((size_t)W * H * 4), *mk = malloc((size_t)W * H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            memcpy(ov + ((size_t)y * W + x) * 4, f.p + ((size_t)y * FW + x) * 4, 4);
            mk[(size_t)y * W + x] = dil[(size_t)y * FW + x];
        }
    char path[4096]; unsigned er;
    snprintf(path, sizeof path, "%s/overlay.png", outdir);
    if ((er = lodepng_encode32_file(path, ov, W, H))) { fprintf(stderr, "cannot write %s: %s\n", path, lodepng_error_text(er)); return 1; }
    snprintf(path, sizeof path, "%s/mask.png", outdir);
    unsigned char *mk3 = malloc((size_t)W * H * 3);
    for (size_t i = 0; i < (size_t)W * H; i++) mk3[i * 3] = mk3[i * 3 + 1] = mk3[i * 3 + 2] = mk[i];
    if ((er = lodepng_encode24_file(path, mk3, W, H))) { fprintf(stderr, "cannot write %s: %s\n", path, lodepng_error_text(er)); return 1; }
    printf("out_w=%d\nout_h=%d\nscale_w=%d\nscale_h=%d\ncrop_x=%d\ncrop_y=%d\ncrop_w=%d\ncrop_h=%d\nplace_x=%d\nplace_y=%d\n",
           W, H, sw, sh, cx, cy, cw2, ch2, px, py);
    return 0;
}

int main(int argc, char **argv) {
    int rc = 1;
    if (argc >= 2 && !strcmp(argv[1], "detect")) rc = cmd_detect(argc, argv);
    else if (argc >= 2 && !strcmp(argv[1], "compose")) rc = cmd_compose(argc, argv);
    else if (argc >= 2 && !strcmp(argv[1], "size")) rc = cmd_size(argc, argv);
    else if (argc >= 2 && !strcmp(argv[1], "vprep")) rc = cmd_vprep(argc, argv);
    if (rc == 1 && argc < 3)
        fprintf(stderr, "usage: frame detect <frame.png> [refW refH]\n"
                        "       frame size <image>\n"
                        "       frame compose <frame.png> <in.png|jpg> <out.png|jpg> [--dx N --dy N --scale F --mode cover|stretch --rot cw|ccw --res native|frame --quality N --bg RRGGBB]\n");
    return rc;
}
