/* Asset loading: Wavefront OBJ meshes and PPM textures (P3 and P6).
 * The engine's world can be code-built or file-built; both paths land
 * in the same Mesh/Tex types. */
#define _POSIX_C_SOURCE 200809L
#include "engine.h"

static char *read_all(const char *path, long *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return NULL; }
    fclose(f);
    buf[n] = 0;
    *out_len = n;
    return buf;
}

Mesh mesh_load_obj(const char *path) {
    Mesh m = { NULL, 0, NULL, 0 };
    long len;
    char *txt = read_all(path, &len);
    if (!txt) return m;
    /* first pass: count */
    int nv = 0, nvt = 0, nvn = 0, nf = 0;
    for (char *p = txt; *p; p++) {
        if (p == txt || p[-1] == '\n') {
            if (p[0] == 'v' && p[1] == ' ') nv++;
            else if (p[0] == 'v' && p[1] == 't') nvt++;
            else if (p[0] == 'v' && p[1] == 'n') nvn++;
            else if (p[0] == 'f' && p[1] == ' ') nf++;
        }
    }
    if (!nv || !nf) { free(txt); return m; }
    float *vs = malloc(sizeof(float) * 3 * (size_t)nv);
    float *vts = malloc(sizeof(float) * 2 * (size_t)(nvt ? nvt : 1));
    float *vns = malloc(sizeof(float) * 3 * (size_t)(nvn ? nvn : 1));
    int iv = 0, ivt = 0, ivn = 0;
    /* expanded (non-indexed) output, upper bound 4-gon faces */
    MVert *out = malloc(sizeof(MVert) * (size_t)nf * 6);
    int nout = 0;
    char *save = NULL;
    for (char *line = strtok_r(txt, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        if (line[0] == 'v' && line[1] == ' ') {
            float x, y, z;
            if (sscanf(line + 2, "%f %f %f", &x, &y, &z) == 3) {
                vs[iv * 3] = x; vs[iv * 3 + 1] = y; vs[iv * 3 + 2] = z; iv++;
            }
        } else if (line[0] == 'v' && line[1] == 't') {
            float u, v;
            if (sscanf(line + 3, "%f %f", &u, &v) == 2) {
                vts[ivt * 2] = u; vts[ivt * 2 + 1] = v; ivt++;
            }
        } else if (line[0] == 'v' && line[1] == 'n') {
            float x, y, z;
            if (sscanf(line + 3, "%f %f %f", &x, &y, &z) == 3) {
                vns[ivn * 3] = x; vns[ivn * 3 + 1] = y; vns[ivn * 3 + 2] = z; ivn++;
            }
        } else if (line[0] == 'f' && line[1] == ' ') {
            int corner[8][3];
            int nc = 0;
            char *fsave = NULL;
            for (char *tok = strtok_r(line + 2, " \r", &fsave); tok && nc < 8;
                 tok = strtok_r(NULL, " \r", &fsave)) {
                int a = 0, b = 0, c = 0;
                if (sscanf(tok, "%d/%d/%d", &a, &b, &c) >= 1) {
                    corner[nc][0] = a; corner[nc][1] = b; corner[nc][2] = c;
                    nc++;
                }
            }
            for (int k = 1; k + 1 < nc; k++) {   /* fan triangulation */
                const int tri_idx[3] = { 0, k, k + 1 };
                for (int j = 0; j < 3; j++) {
                    int ci = corner[tri_idx[j]][0] - 1;
                    int ti = corner[tri_idx[j]][1] - 1;
                    int ni = corner[tri_idx[j]][2] - 1;
                    if (ci < 0 || ci >= nv) continue;
                    MVert *mv = &out[nout++];
                    mv->p = v3(vs[ci * 3], vs[ci * 3 + 1], vs[ci * 3 + 2]);
                    mv->u = (ti >= 0 && ti < nvt) ? vts[ti * 2] : 0;
                    mv->v = (ti >= 0 && ti < nvt) ? vts[ti * 2 + 1] : 0;
                    mv->n = (ni >= 0 && ni < nvn)
                        ? v3(vns[ni * 3], vns[ni * 3 + 1], vns[ni * 3 + 2])
                        : v3(0, 1, 0);
                }
            }
        }
    }
    m.verts = out;
    m.nv = nout;
    m.nt = nout / 3;
    m.tris = malloc(sizeof(MTri) * (size_t)(m.nt ? m.nt : 1));
    for (int i = 0; i < m.nt; i++) m.tris[i] = (MTri){ i * 3, i * 3 + 1, i * 3 + 2 };
    free(vs); free(vts); free(vns); free(txt);
    return m;
}

int tex_load_ppm(const char *path, Tex *out) {
    long len;
    char *buf = read_all(path, &len);
    if (!buf) return -1;
    if (len < 3 || buf[0] != 'P' || (buf[1] != '3' && buf[1] != '6')) {
        free(buf); return -1;
    }
    int is_p3 = buf[1] == '3';
    const char *p = buf + 2;
    const char *end = buf + len;
    int fields[3], nf = 0;
    while (nf < 3 && p < end) {          /* header: width height maxval */
        if (*p == '#') { while (p < end && *p != '\n') p++; }
        else if (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
        else {
            char *nx;
            fields[nf++] = (int)strtol(p, &nx, 10);
            p = nx;
        }
    }
    if (nf < 3 || fields[0] <= 0 || fields[1] <= 0 || fields[2] <= 0) {
        free(buf); return -1;
    }
    out->w = fields[0];
    out->h = fields[1];
    int maxval = fields[2];
    out->px = malloc(sizeof(uint32_t) * (size_t)(out->w * out->h));
    if (is_p3) {
        for (int i = 0; i < out->w * out->h; i++) {
            int rgb[3];
            for (int c = 0; c < 3; c++) {
                while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
                if (p >= end) { free(out->px); free(buf); return -1; }
                char *nx;
                rgb[c] = (int)strtol(p, &nx, 10) * 255 / maxval;
                p = nx;
            }
            out->px[i] = ((uint32_t)rgb[0] << 16) | ((uint32_t)rgb[1] << 8) | (uint32_t)rgb[2];
        }
    } else {
        p++;                              /* single whitespace before raster */
        if ((end - p) < (long)(out->w * out->h * 3)) { free(out->px); free(buf); return -1; }
        for (int i = 0; i < out->w * out->h; i++) {
            int r = (unsigned char)p[i * 3] * 255 / maxval;
            int g = (unsigned char)p[i * 3 + 1] * 255 / maxval;
            int b = (unsigned char)p[i * 3 + 2] * 255 / maxval;
            out->px[i] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
    free(buf);
    return 0;
}
