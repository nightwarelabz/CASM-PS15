/* Asset loading: Wavefront OBJ meshes (full v / v/vt / v//vn / v/vt/vn +
 * negative relative indices + arbitrary n-gon fan triangulation) and PPM
 * textures (P3 ASCII and P6 binary). */
#define _POSIX_C_SOURCE 200809L
#include "engine.h"

static char *read_all(const char *path, long *out_len) {
    if (!path) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    if (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f); free(buf); return NULL;
    }
    fclose(f);
    buf[n] = 0;
    *out_len = n;
    return buf;
}

static int resolve_obj_idx(int raw, int count) {
    if (raw > 0) return raw - 1;
    if (raw < 0) return count + raw;
    return -1;
}

static bool parse_obj_corner(const char *tok, int *vi, int *ti, int *ni) {
    *vi = 0; *ti = 0; *ni = 0;
    char *end = NULL;
    long v = strtol(tok, &end, 10);
    if (end == tok || v == 0) return false;
    *vi = (int)v;
    if (*end == '/') {
        const char *p = end + 1;
        if (*p != '/' && *p != '\0') {
            *ti = (int)strtol(p, &end, 10);
            p = end;
        }
        if (*p == '/') {
            *ni = (int)strtol(p + 1, &end, 10);
        }
    }
    return true;
}

Mesh mesh_load_obj(const char *path) {
    Mesh m = { NULL, 0, NULL, 0, 0.0f, { 0, 0, 0 } };
    long len = 0;
    char *txt = read_all(path, &len);
    if (!txt) return m;

    int nv = 0, nvt = 0, nvn = 0, nf = 0;
    for (char *p = txt; *p; p++) {
        if (p == txt || p[-1] == '\n') {
            while (*p == ' ' || *p == '\t') p++;
            if (p[0] == 'v' && p[1] == ' ') nv++;
            else if (p[0] == 'v' && p[1] == 't' && (p[2] == ' ' || p[2] == '\t')) nvt++;
            else if (p[0] == 'v' && p[1] == 'n' && (p[2] == ' ' || p[2] == '\t')) nvn++;
            else if (p[0] == 'f' && (p[1] == ' ' || p[1] == '\t')) nf++;
            if (!*p) break;
        }
    }
    if (!nv || !nf) { free(txt); return m; }

    float *vs  = calloc((size_t)nv * 3, sizeof(float));
    float *vts = calloc((size_t)(nvt ? nvt : 1) * 2, sizeof(float));
    float *vns = calloc((size_t)(nvn ? nvn : 1) * 3, sizeof(float));
    int iv = 0, ivt = 0, ivn = 0;

    int cap_out = nf * 6;
    MVert *out = malloc(sizeof(MVert) * (size_t)cap_out);
    int nout = 0;

    char *save = NULL;
    for (char *line = strtok_r(txt, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        while (*line == ' ' || *line == '\t') line++;
        if (line[0] == '#' || line[0] == '\0' || line[0] == '\r') continue;
        if (line[0] == 'v' && line[1] == ' ') {
            float x, y, z;
            if (iv < nv && sscanf(line + 2, "%f %f %f", &x, &y, &z) == 3) {
                vs[iv * 3] = x; vs[iv * 3 + 1] = y; vs[iv * 3 + 2] = z;
                iv++;
            }
        } else if (line[0] == 'v' && line[1] == 't' && (line[2] == ' ' || line[2] == '\t')) {
            float u, v;
            if (ivt < nvt && sscanf(line + 3, "%f %f", &u, &v) == 2) {
                vts[ivt * 2] = u; vts[ivt * 2 + 1] = v;
                ivt++;
            }
        } else if (line[0] == 'v' && line[1] == 'n' && (line[2] == ' ' || line[2] == '\t')) {
            float x, y, z;
            if (ivn < nvn && sscanf(line + 3, "%f %f %f", &x, &y, &z) == 3) {
                vns[ivn * 3] = x; vns[ivn * 3 + 1] = y; vns[ivn * 3 + 2] = z;
                ivn++;
            }
        } else if (line[0] == 'f' && (line[1] == ' ' || line[1] == '\t')) {
            int corner[32][3];
            int nc = 0;
            char *fsave = NULL;
            for (char *tok = strtok_r(line + 2, " \t\r", &fsave); tok && nc < 32;
                 tok = strtok_r(NULL, " \t\r", &fsave)) {
                if (tok[0] == '#') break;
                int a, b, c;
                if (parse_obj_corner(tok, &a, &b, &c)) {
                    corner[nc][0] = a; corner[nc][1] = b; corner[nc][2] = c;
                    nc++;
                }
            }
            for (int k = 1; k + 1 < nc; k++) {
                const int tri_idx[3] = { 0, k, k + 1 };
                int ci[3], ti[3], ni[3];
                bool valid = true;
                for (int j = 0; j < 3; j++) {
                    ci[j] = resolve_obj_idx(corner[tri_idx[j]][0], iv);
                    ti[j] = resolve_obj_idx(corner[tri_idx[j]][1], ivt);
                    ni[j] = resolve_obj_idx(corner[tri_idx[j]][2], ivn);
                    if (ci[j] < 0 || ci[j] >= iv) valid = false;
                }
                if (!valid) continue;

                V3 p0 = v3(vs[ci[0]*3], vs[ci[0]*3+1], vs[ci[0]*3+2]);
                V3 p1 = v3(vs[ci[1]*3], vs[ci[1]*3+1], vs[ci[1]*3+2]);
                V3 p2 = v3(vs[ci[2]*3], vs[ci[2]*3+1], vs[ci[2]*3+2]);
                V3 auto_n = v3_norm(v3_cross(v3_sub(p1, p0), v3_sub(p2, p0)));
                const V3 pts[3] = { p0, p1, p2 };

                if (nout + 3 > cap_out) {
                    cap_out *= 2;
                    out = realloc(out, sizeof(MVert) * (size_t)cap_out);
                }
                for (int j = 0; j < 3; j++) {
                    MVert *mv = &out[nout++];
                    mv->p = pts[j];
                    mv->u = (ti[j] >= 0 && ti[j] < ivt) ? vts[ti[j] * 2]     : 0.0f;
                    mv->v = (ti[j] >= 0 && ti[j] < ivt) ? vts[ti[j] * 2 + 1] : 0.0f;
                    mv->n = (ni[j] >= 0 && ni[j] < ivn)
                        ? v3(vns[ni[j]*3], vns[ni[j]*3+1], vns[ni[j]*3+2])
                        : auto_n;
                }
            }
        }
    }

    free(vs); free(vts); free(vns); free(txt);
    if (nout < 3) { free(out); return m; }

    m.verts = out;
    m.nv = nout;
    m.nt = nout / 3;
    m.tris = malloc(sizeof(MTri) * (size_t)m.nt);
    for (int i = 0; i < m.nt; i++)
        m.tris[i] = (MTri){ i * 3, i * 3 + 1, i * 3 + 2 };
    mesh_compute_bounds(&m);
    return m;
}

int tex_load_ppm(const char *path, Tex *out) {
    if (!path || !out) return -1;
    long len = 0;
    char *buf = read_all(path, &len);
    if (!buf) return -1;
    if (len < 3 || buf[0] != 'P' || (buf[1] != '3' && buf[1] != '6')) {
        free(buf); return -1;
    }
    int is_p3 = (buf[1] == '3');
    const char *p = buf + 2;
    const char *end = buf + len;
    int fields[3] = { 0, 0, 0 }, nf = 0;
    while (nf < 3 && p < end) {
        if (*p == '#') {
            while (p < end && *p != '\n') p++;
        } else if (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
            p++;
        } else {
            char *nx = NULL;
            long val = strtol(p, &nx, 10);
            if (nx == p || val <= 0 || val > 16384) { free(buf); return -1; }
            fields[nf++] = (int)val;
            p = nx;
        }
    }
    if (nf < 3 || fields[2] > 65535) { free(buf); return -1; }

    out->w = fields[0];
    out->h = fields[1];
    int maxval = fields[2];
    size_t npix = (size_t)out->w * (size_t)out->h;
    if (npix > 16777216u) { free(buf); return -1; }
    out->px = malloc(sizeof(uint32_t) * npix);
    if (!out->px) { free(buf); return -1; }

    if (is_p3) {
        for (size_t i = 0; i < npix; i++) {
            int rgb[3];
            for (int c = 0; c < 3; c++) {
                while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' ||
                                   *p == '\t' || *p == '#')) {
                    if (*p == '#') while (p < end && *p != '\n') p++;
                    else p++;
                }
                if (p >= end) { free(out->px); out->px = NULL; free(buf); return -1; }
                char *nx = NULL;
                long v = strtol(p, &nx, 10);
                if (nx == p) { free(out->px); out->px = NULL; free(buf); return -1; }
                if (v < 0) v = 0;
                if (v > maxval) v = maxval;
                rgb[c] = (int)(v * 255 / maxval);
                p = nx;
            }
            out->px[i] = ((uint32_t)rgb[0] << 16) |
                         ((uint32_t)rgb[1] << 8)  |
                         (uint32_t)rgb[2];
        }
    } else {
        if (p >= end) { free(out->px); out->px = NULL; free(buf); return -1; }
        p++; /* single whitespace delimiter before binary raster */
        if ((size_t)(end - p) < npix * 3) {
            free(out->px); out->px = NULL; free(buf); return -1;
        }
        for (size_t i = 0; i < npix; i++) {
            int r = (int)((unsigned char)p[i * 3]     * 255 / maxval);
            int g = (int)((unsigned char)p[i * 3 + 1] * 255 / maxval);
            int b = (int)((unsigned char)p[i * 3 + 2] * 255 / maxval);
            out->px[i] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
    free(buf);
    return 0;
}
