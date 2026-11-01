/* Procedural painted PS1.5 textures (128x128, deterministic, zero image deps):
 *   TEX_BRICK    — cold-war dark oxblood/soot-umber brick with recessed mortar
 *   TEX_CONCRETE — wet Greyport slate-asphalt slabs with expansion seams & sheen
 *   TEX_METAL    — dark corrugated & riveted industrial steel plate
 *   TEX_CAMO     — disciplined matte-charcoal urban night pixel camo (DeBrief)
 *   TEX_FACE     — pixel-art brow + smoked-ruby mosaic visor + pinpoint red LED
 *   TEX_VEST     — MOLLE plate carrier, pouches, tactical glove & guard gear
 *   TEX_WINDOW   — recessed mullioned casement windows (warm tungsten + night glass)
 *   TEX_HAZARD   — dark corrugated container/seawall steel + weathered ochre band
 *   TEX_GRATE    — color-keyed (TEX_MASK_BIT) steel catwalk / railing lattice
 *   TEX_SHADOW   — radial falloff map for BLEND_SUB shadows & wet reflections
 *   TEX_PATCH    — dedicated 128x128 gold-bordered NSA eagle shoulder crest
 *   TEX_WEAPON   — brushed steel slide, polymer grip & baffles
 *   TEX_WATER    — tileable harbor canal waves & specular ripples
 *   TEX_BEAM     — smooth volumetric searchlight & muzzle falloff map
 *   TEX_TERMINAL — classified espionage CRT intelligence terminal screen
 *   TEX_INSIGNIA — Regime 3-bar banner/armband, trooper mask & mecha plate
 *
 * 128x128 modern tier: every painter keeps its 64-era layout at the same
 * world scale (feature coordinates doubled) and spends the extra texels on
 * a second octave of hand-placed detail — chips, cracks, rust weep, curtain
 * bands, aggregate speckle — at native resolution. */
#include "engine.h"

#define TS 128

static uint32_t pack(int r, int g, int b) {
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static int hash2(int x, int y, uint32_t seed) {
    uint32_t h = ((uint32_t)x * 374761393u + (uint32_t)y * 668265263u) ^ seed;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (int)((h ^ (h >> 16)) & 255u);
}

/* High-contrast 4-tier digital urban pixel camo (matches operator-model-sheet.jpg
 * and DeBrief banner so 3D polygon facet lighting reads with razor-sharp punch!) */
static uint32_t camo_pixel_sz(int x, int y, int cell) {
    int bx = x / cell, by = y / cell;
    int macro = hash2(x / (cell * 2 + 1), y / (cell * 2), 0xCA40u);
    int mid   = hash2(bx, by, 0x51A7u);
    int fine  = hash2(x, y, 0x9911u) & 3;
    int sel = (macro * 2 + mid * 3) / 5;
    int r, g, b;
    if (sel > 186) {
        /* Crisp slate-gray digital highlight clusters */
        r = 86 + fine * 3; g = 90 + fine * 3; b = 100 + fine * 3;
    } else if (sel > 124) {
        /* Medium graphite digital clusters */
        r = 56 + fine * 2; g = 60 + fine * 2; b = 68 + fine * 2;
    } else if (sel > 58) {
        /* Dark charcoal night clusters */
        r = 34 + fine;     g = 37 + fine;     b = 43 + fine;
    } else {
        /* Deep midnight shadow clusters */
        r = 19 + fine / 2; g = 21 + fine / 2; b = 25 + fine / 2;
    }
    return pack(r, g, b);
}

static uint32_t camo_pixel(int x, int y) {
    return camo_pixel_sz(x, y, 6);
}

/* Crisp 7x9 pixel-art "N", "S", "A" glyphs for TEX_PATCH scroll banner */
static bool patch_letter(char ch, int lx, int ly) {
    if (lx < 0 || lx > 6 || ly < 0 || ly > 8) return false;
    if (ch == 'N') {
        if (lx <= 1 || lx >= 5) return true;
        int diag = (ly * 4 + 2) / 8 + 1;
        return (lx == diag || lx == diag + 1);
    }
    if (ch == 'S') {
        if (ly <= 1 && lx >= 1 && lx <= 5) return true;
        if (ly >= 1 && ly <= 4 && lx <= 1) return true;
        if ((ly == 3 || ly == 4) && lx >= 1 && lx <= 5) return true;
        if (ly >= 4 && ly <= 7 && lx >= 5) return true;
        if (ly >= 7 && lx >= 1 && lx <= 5) return true;
        return false;
    }
    if (ch == 'A') {
        if (ly <= 1 && lx >= 2 && lx <= 4) return true;
        if (ly >= 2 && (lx <= 1 || lx >= 5)) return true;
        if ((ly == 4 || ly == 5) && lx >= 1 && lx <= 5) return true;
        return false;
    }
    return false;
}

void texgen_all(Tex out[TEX_COUNT]) {
    for (int k = 0; k < TEX_COUNT; k++) {
        out[k].w = out[k].h = TS;
        out[k].px = malloc(sizeof(uint32_t) * TS * TS);
    }

    for (int y = 0; y < TS; y++) {
        for (int x = 0; x < TS; x++) {
            int idx = y * TS + x;
            int n = hash2(x, y, 7);

            /* 1. TEX_BRICK: cold-war dark nocturne slate-umber masonry (greyport nocturne) */
            {
                int row = y / 16;
                int off = (row & 1) ? 32 : 0;
                int bx = (x + off) % 64;
                int by = y % 16;
                bool mortar = (by <= 1) || (bx <= 1);
                int brick_id = (x + off) / 64;
                int bv = (hash2(brick_id, row, 21) % 16) - 8;
                if (mortar) {
                    int mv = 20 + (n & 3);
                    out[TEX_BRICK].px[idx] = pack(mv, mv + 2, mv + 5);
                } else {
                    int bevel = (by == 15 ? 6 : (by == 2 ? -6 : 0)) +
                                (bx == 2 ? 3 : (bx == 63 ? -3 : 0));
                    int soot = (y < 24) ? (24 - y) : 0;
                    int fine = (n & 7) - 3;
                    /* 128-tier craft: chipped faces + spall speckle */
                    int chip = (hash2(x, y, 99) > 238) ? -14 : 0;
                    int spall = (hash2(x / 2, y / 2, 55) > 244) ? 9 : 0;
                    int v = 46 + bv + bevel + fine - soot + chip + spall;
                    out[TEX_BRICK].px[idx] = pack(
                        v + 2,
                        v - 4,
                        v - 2);
                }
            }

            /* 2. TEX_CONCRETE: smooth dark wet slate-asphalt slabs + tar expansion joints */
            {
                bool seam = (x % 64 <= 1 || y % 64 <= 1);
                int grain = (hash2(x, y, 0x4812u) & 7) - 3;
                int macro = (hash2(x / 16, y / 16, 91) - 128) / 16;
                int puddle = hash2(x / 8, y / 8, 137);
                int fine = (n & 3) - 1;
                /* 128-tier craft: aggregate speckle + hairline cracks */
                int agg = (hash2(x, y, 0x77AAu) > 248) ? 10 : 0;
                int crack = (hash2(x, y / 8, 733) > 250) ? -12 : 0;
                int v = 34 + macro + fine + grain + agg + crack;
                if (seam) {
                    out[TEX_CONCRETE].px[idx] = pack(16, 18, 24);
                } else if (puddle > 188) {
                    /* Smooth dark wet puddle patch reflecting nocturnal sky */
                    out[TEX_CONCRETE].px[idx] = pack(v - 8, v - 4, v + 9);
                } else {
                    out[TEX_CONCRETE].px[idx] = pack(v, v + 3, v + 7);
                }
            }

            /* 3. TEX_METAL: dark slate-charcoal corrugated industrial steel */
            {
                bool border = (x < 4 || y < 4 || x > 123 || y > 123);
                bool seam_h = (y >= 62 && y <= 65);
                static const int rv_x[6] = { 8, 118, 8, 118, 8, 118 };
                static const int rv_y[6] = { 8, 8, 56, 70, 118, 118 };
                bool rivet = false, weep = false;
                for (int ri = 0; ri < 6; ri++) {
                    int dx = x - rv_x[ri], dy = y - rv_y[ri];
                    if (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1) rivet = true;
                    /* Rust weep: stained streak bleeding down from each rivet */
                    if (dx >= -1 && dx <= 1 && dy > 1 && dy <= 20 &&
                        hash2(x, y, 0xF00Du) > 120) weep = true;
                }
                int rib = ((x % 16) < 6) ? 6 : ((x % 16) <= 7 ? 11 : -5);
                int fine = (n & 5) - 2;
                /* 128-tier craft: brushed scratch lines on rare rows */
                int scratch = (hash2(y, 3, 0x5C5Cu) == 77 && (x % 37) < 24) ? 16 : 0;
                int v = 49 + rib + fine - (y < 16 ? (16 - y) / 2 : 0) + scratch;
                if (weep) v -= 6;
                out[TEX_METAL].px[idx] = rivet ? pack(112, 118, 128)
                    : (border || seam_h) ? pack(26, 29, 36)
                    : weep ? pack(v - 4, v - 8, v - 2)
                    : pack(v, v + 4, v + 11);
            }

            /* 4. TEX_CAMO: matte charcoal/graphite pixel camo (finer cells at 128) */
            out[TEX_CAMO].px[idx] = camo_pixel(x, y);

            /* 5. TEX_FACE: sculpted pixel-art brow + ruby mosaic visor + red LED */
            {
                uint32_t c = camo_pixel_sz(x, y, 4);
                if (x >= 6 && x <= 18 && y >= 6 && y <= 18) {
                    int mv = 120 + ((x + y) & 3) * 8;
                    c = pack(mv, mv + 2, mv + 6); /* brushed gunmetal boom-mic arm */
                } else if (x >= 24 && x <= 102) {
                    int hair_dip = (abs(x - 64) <= 8) ? -2 : ((hash2(x / 4, 3, 0x4A12u) > 150) ? 2 : 0);
                    if (y >= 8 && y <= 48) {
                        /* Matte charcoal pixel camo balaclava matching jacket & hood */
                        uint32_t mc = camo_pixel_sz(x, y, 4);
                        int mr = ((int)(mc >> 16) & 255);
                        int mg = ((int)(mc >> 8)  & 255);
                        int mb = ((int)(mc)       & 255);
                        int ridge = (abs(x - 64) <= 2) ? 7 : (abs(x - 64) > 24 ? -4 : 0);
                        int seam  = (y == 48 || y == 26) ? -6 : 0;
                        c = pack(mr + ridge + seam, mg + ridge + seam, mb + ridge + seam + 2);
                    } else if (y >= 50 && y <= 76) {
                        bool frame = (y <= 52 || y >= 74 || x <= 28 || x >= 98);
                        if (frame) {
                            int fv = 14 + ((y == 74) ? 8 : 0) + (n & 3);
                            c = pack(fv, fv + 2, fv + 5);
                        } else {
                            /* Deep smoked-ruby 2x2 pixel mosaic lens + outer edge glints */
                            int cx2 = x / 2, cy2 = y / 2;
                            int cell = hash2(cx2, cy2, 0x7150u);
                            bool edge_glint = ((cx2 >= 16 && cx2 <= 20 && cy2 >= 32) ||
                                               (cx2 >= 42 && cx2 <= 46 && cy2 <= 30)) &&
                                              ((cx2 + cy2) & 1) == 0;
                            int lr, lg, lb;
                            if (edge_glint) {
                                lr = 155; lg = 58; lb = 72;
                            } else if (cell > 192) {
                                lr = 112; lg = 24; lb = 34;
                            } else if (cell > 118) {
                                lr = 82;  lg = 16; lb = 24;
                            } else if (cell > 52) {
                                lr = 52;  lg = 10; lb = 16;
                            } else {
                                lr = 30;  lg = 6;  lb = 10;
                            }
                            /* Razor-sharp scarlet-red optic LED slightly left of center */
                            if (x >= 56 && x <= 62 && y >= 62 && y <= 68) {
                                bool core = (x >= 58 && x <= 60 && y >= 64 && y <= 66);
                                lr = core ? 255 : 210;
                                lg = core ?  60 :  20;
                                lb = core ?  52 :  24;
                            }
                            c = pack(lr, lg, lb);
                        }
                    } else if (y >= 78 && y <= (98 + hair_dip) && x >= 30 && x <= 96) {
                        /* Authentic cold-war portrait brow: warm left key-light -> deep right shadow */
                        int bx = (x / 2) * 2;
                        int by = (y / 2) * 2;
                        float t_side = (float)(bx - 30) / 66.0f; /* 0.0 left -> 1.0 right */
                        int base_r = (int)lerpf(186.0f, 82.0f, t_side);
                        int base_g = (int)lerpf(132.0f, 52.0f, t_side);
                        int base_b = (int)lerpf( 92.0f, 36.0f, t_side);
                        int dist_c = abs(bx - 62);
                        int hood_sh = (by >= 94) ? 28 : (by >= 90 ? 14 : 0);
                        int furrow = (dist_c <= 2 && by <= 88) ? 18 : 0;
                        int cell_s = (hash2(bx, by, 0x5B19u) & 7) - 3;
                        int sr = clampf(base_r - hood_sh - furrow + cell_s, 20, 240);
                        int sg = clampf(base_g - (hood_sh * 3)/4 - (furrow * 3)/4 + cell_s / 2, 15, 180);
                        int sb = clampf(base_b - (hood_sh * 2)/3 - (furrow * 2)/3 + cell_s / 2, 10, 140);
                        c = pack(sr, sg, sb);
                    }
                }
                out[TEX_FACE].px[idx] = c;
            }

            /* 6. TEX_VEST: 4-quadrant MOLLE vest, camo sleeve, glove & guard */
            if (y < 64 && x < 64) {
                bool strap = (y % 8 == 0);
                bool pals  = (!strap && (x % 12 == 0));
                bool pouch = (y >= 6 && y <= 34 &&
                              ((x >= 6 && x <= 20) || (x >= 24 && x <= 38) ||
                               (x >= 42 && x <= 56)));
                bool tab = pouch && (y >= 28 && y <= 30) && (((x - 6) % 18) >= 6 && ((x - 6) % 18) <= 8);
                int v = 28 + (n & 3) + (strap ? -9 : 0) + (pals ? -5 : 0) + (pouch ? 12 : 0);
                out[TEX_VEST].px[idx] = tab ? pack(105, 110, 122)
                                            : pack(v, v + 3, v + 7);
            } else if (y < 64 && x >= 64) {
                out[TEX_VEST].px[idx] = camo_pixel_sz(x, y, 4);
            } else if (y >= 64 && x < 64) {
                int py = y - 64;
                if (py >= 32) {
                    bool serration = (x >= 6 && x <= 22 && (x & 1));
                    int sv = 132 + (py - 40) + (n & 5) - (serration ? 34 : 0);
                    out[TEX_VEST].px[idx] = pack(sv, sv + 4, sv + 10);
                } else if (x < 32) {
                    /* Dark brushed gunmetal steel with knurled micro-texture for tactical suppressor */
                    bool knurl = ((x + py) & 1) != 0;
                    bool rib   = (x % 6 == 0);
                    int gmv = 82 + (knurl ? 20 : -8) + (rib ? 18 : 0) + (n & 5);
                    out[TEX_VEST].px[idx] = pack(gmv, gmv + 3, gmv + 8);
                } else {
                    /* Combat boot front laces, metal eyelets & rugged lug sole */
                    int bx = x - 32; /* 0..31 */
                    bool eyelet = (bx == 4 || bx == 26) && (py % 6 == 2);
                    bool lace = ((abs(bx - 15) == abs(py % 12 - 6)) && (bx >= 6 && bx <= 24));
                    bool sole = (py >= 24);
                    bool lug = sole && (bx % 8 < 4);
                    if (eyelet) {
                        out[TEX_VEST].px[idx] = pack(185, 190, 202);
                    } else if (lace) {
                        out[TEX_VEST].px[idx] = pack(20, 22, 28);
                    } else if (sole) {
                        int lv = lug ? 16 : 28;
                        out[TEX_VEST].px[idx] = pack(lv, lv + 2, lv + 5);
                    } else {
                        uint32_t gc = camo_pixel_sz(x, py, 4);
                        int gr = ((int)(gc >> 16) & 255) * 4 / 5;
                        int gg = ((int)(gc >> 8)  & 255) * 4 / 5;
                        int gb = ((int)(gc)       & 255) * 4 / 5;
                        out[TEX_VEST].px[idx] = pack(gr, gg + 1, gb + 3);
                    }
                }
            } else {
                int px = x - 64, py = y - 64;
                bool stud   = (px >= 44 && px <= 58 && py >= 4  && py <= 18);
                bool buckle = (px >= 44 && px <= 58 && py >= 24 && py <= 38);
                bool strap  = (py >= 48 && py <= 62);
                bool clasp  = strap && (px >= 28 && px <= 36);

                if (stud) {
                    /* Polished silver double-breasted snap button stud */
                    int dx = px - 51, dy = py - 11;
                    int r2 = dx * dx + dy * dy;
                    if (r2 <= 32) {
                        int h = (dx < 0 && dy < 0) ? 42 : (dx > 0 && dy > 0 ? -32 : 0);
                        int sv = 232 + h + (n & 7);
                        out[TEX_VEST].px[idx] = pack(sv, sv + 3, sv + 9);
                    } else {
                        out[TEX_VEST].px[idx] = pack(24, 26, 32);
                    }
                } else if (buckle) {
                    /* Rectangular metallic harness buckle */
                    bool rim = (px == 44 || px == 58 || py == 24 || py == 38 || px == 51);
                    int bv = rim ? 212 + (n & 7) : 26;
                    out[TEX_VEST].px[idx] = pack(bv, bv + 3, bv + 8);
                } else if (strap) {
                    /* Horizontal combat boot shin buckle strap */
                    if (clasp) {
                        int cv = 208 + (n & 7);
                        out[TEX_VEST].px[idx] = pack(cv, cv + 4, cv + 9);
                    } else {
                        int stv = (py == 48 || py == 62) ? 18 : 34 + (n & 3);
                        out[TEX_VEST].px[idx] = pack(stv, stv + 2, stv + 4);
                    }
                } else {
                    /* Solid dark charcoal wool greatcoat fabric */
                    int cell = hash2(px / 8, py / 8, 0x60A2u);
                    int gv = 36 + cell / 10 + (n & 3);
                    out[TEX_VEST].px[idx] = pack(gv, gv + 3, gv + 5);
                }
            }

            /* 7. TEX_WINDOW: recessed stone-framed casement windows (warm + night) */
            {
                int wx = x % 64, wy = y % 64;
                bool in_win = (wx >= 12 && wx <= 50 && wy >= 12 && wy <= 54);
                if (!in_win) {
                    bool sill = (wy >= 8 && wy <= 10 && wx >= 8 && wx <= 54);
                    bool lintel = (wy >= 56 && wy <= 58 && wx >= 8 && wx <= 54);
                    int wv = 32 + (n & 5) + (sill ? 14 : (lintel ? 10 : 0));
                    out[TEX_WINDOW].px[idx] = pack(wv + 2, wv + 2, wv + 6);
                } else {
                    bool mullion = (wx == 12 || wx == 30 || wx == 32 || wx == 50 ||
                                    wy == 12 || wy == 32 || wy == 34 || wy == 54);
                    int pane_id = (x / 64) * 2 + (y / 64);
                    bool lit = (pane_id == 0 || pane_id == 3);
                    if (mullion) {
                        out[TEX_WINDOW].px[idx] = pack(18, 21, 28);
                    } else if (lit) {
                        int grad = 212 - abs(wx - 31) * 3 - (54 - wy) * 2 + (n & 7);
                        /* 128-tier craft: curtain bands shadow the lit glass */
                        if ((wx % 16) < 3) grad -= 38;
                        out[TEX_WINDOW].px[idx] = pack(grad + 22, (grad * 3) / 4 + 8, grad / 3);
                    } else {
                        int dv = 18 + (wy - 12) / 4 + (n & 3);
                        /* 128-tier craft: faint diagonal sky reflection streak */
                        if (((wx + wy) % 48) < 3) dv += 12;
                        out[TEX_WINDOW].px[idx] = pack(dv, dv + 6, dv + 16);
                    }
                }
            }

            /* 8. TEX_HAZARD: dark corrugated container/seawall + weathered ochre band */
            {
                bool seam = (x <= 1 || x >= 126 || y <= 1 || y >= 126);
                int rib = ((x % 12) < 4) ? 6 : ((x % 12) <= 5 ? 9 : -4);
                int damp = (y < 20) ? (20 - y) : 0;
                int wear = hash2(x, y, 0x4A2Au);
                if (y >= 26 && y <= 34 && wear > 46) {
                    /* Muted weathered ochre horizontal stencil stripe (DeBrief quay!) */
                    int yv = 142 + rib / 2 + (n & 7) - (wear < 96 ? 28 : 0);
                    out[TEX_HAZARD].px[idx] = pack(yv + 16, yv - 14, 38);
                } else {
                    /* 128-tier craft: damp drip streaks under the band */
                    int drip = (y > 34 && y < 92 && hash2(x, 5, 0xD12Bu) > 236) ? -8 : 0;
                    int cv = 38 + rib + (n & 5) - damp - (seam ? 12 : 0) + drip;
                    out[TEX_HAZARD].px[idx] = pack(cv, cv + 4, cv + 11);
                }
            }

            /* 9. TEX_GRATE: color-keyed (TEX_MASK_BIT) dark steel catwalk/railing */
            {
                bool top_rail = (y >= 118 || y <= 7);
                bool post = (x < 6 || x >= 122 || (x >= 60 && x <= 66));
                bool mid_bar = (y >= 60 && y <= 64);
                bool rod = ((x % 16) <= 1);
                if (top_rail || post || mid_bar || rod) {
                    int gv = (top_rail ? 68 : 46) + (n & 7);
                    out[TEX_GRATE].px[idx] = pack(gv, gv + 4, gv + 10);
                } else {
                    out[TEX_GRATE].px[idx] = TEX_MASK_BIT;
                }
            }

            /* 10. TEX_SHADOW: smooth radial blob for BLEND_SUB & reflections */
            {
                float dx = ((float)x - 63.5f) / 63.5f;
                float dy = ((float)y - 63.5f) / 63.5f;
                float d = 1.0f - (dx * dx + dy * dy);
                if (d < 0.0f) d = 0.0f;
                int sv = (int)(d * d * 185.0f);
                out[TEX_SHADOW].px[idx] = pack(sv, sv, sv);
            }

            /* 11. TEX_PATCH: authentic embroidered NSA eagle shoulder crest on dark navy shield */
            {
                uint32_t c = camo_pixel(x, y);
                int cx = abs(x - 64);
                /* Shield contour: width 88 at top (cx <= 44), gently tapering to bottom point */
                int shield_w = (y <= 84) ? 44 : (44 - ((y - 84) * 22) / 30);
                if (cx <= shield_w && y >= 12 && y <= 114) {
                    bool border = (cx >= shield_w - 2 || y <= 14 || y >= 112 ||
                                   (y >= 82 && y <= 84 && cx >= 32));
                    if (border) {
                        int gv = ((x + y) & 1) ? 14 : -8;
                        c = pack(198 + gv, 156 + gv, 64 + gv / 2); /* rich embroidered gold */
                    } else if (y >= 86) {
                        /* Lower scroll banner: crisp stark white ribbon with bold pitch black "NSA" */
                        bool rib_edge = (y == 86 || y >= 110 || cx >= shield_w - 4);
                        if (rib_edge) {
                            c = pack(18, 22, 34); /* dark separator border */
                        } else {
                            int ly = (y - 90) / 2;
                            bool ink = patch_letter('N', (x - 38) / 2, ly) ||
                                       patch_letter('S', (x - 58) / 2, ly) ||
                                       patch_letter('A', (x - 78) / 2, ly);
                            if (ink) {
                                c = pack(4, 6, 12); /* bold dark ink lettering */
                            } else {
                                c = pack(255, 255, 252); /* clean bright white scroll ribbon */
                            }
                        }
                    } else {
                        /* Upper shield: deep rich navy ground + golden eagle & national shield */
                        c = pack(18 + (n & 3), 26 + (n & 3), 56 + (n & 5));
                        /* Golden eagle outstretched wings */
                        int wing_top = 26 + (cx * 2) / 7;
                        int wing_bot = 64 - cx / 3;
                        if (cx <= 38 && y >= wing_top && y <= wing_bot) {
                            bool tip = (y <= wing_top + 2) || ((x + y) % 3 == 0);
                            c = tip ? pack(248, 204, 88) : pack(178, 132, 52);
                        }
                        /* Eagle body & talons */
                        if (cx <= 12 && y >= 66 && y <= 74) {
                            c = (cx & 1) ? pack(168, 122, 52) : pack(218, 172, 78);
                        }
                        /* Bald eagle head & golden beak */
                        if (y >= 18 && y <= 32 && cx <= 8) {
                            if (x >= 66 && x <= 72 && y >= 24 && y <= 28)
                                c = pack(238, 185, 48); /* gold beak */
                            else if (x == 66 && y == 24)
                                c = pack(20, 22, 30);   /* dark eye */
                            else
                                c = pack(245, 245, 250); /* white feathered head */
                        }
                        /* National breast shield (blue chief + red & white stripes) */
                        if (cx <= 10 && y >= 38 && y <= 68 && (y <= 62 || cx <= (74 - y))) {
                            if (cx == 10 || y == 38 || y == 68) {
                                c = pack(218, 172, 64); /* gold shield rim */
                            } else if (y <= 50) {
                                c = pack(28, 56, 134);  /* vivid cobalt chief */
                            } else {
                                bool white_stripe = ((cx / 2) % 2 == 0);
                                c = white_stripe ? pack(242, 242, 248) : pack(202, 32, 34);
                            }
                        }
                    }
                }
                out[TEX_PATCH].px[idx] = c;
            }

            /* 12. TEX_WEAPON: two-tone stainless steel USP slide, polymer frame & glove */
            if (y >= 64) {
                int py = y - 64;
                int grain = (hash2(x / 10, y, 0x51DEu) & 7) - 3;
                bool rear_serr = (x >= 14 && x <= 44 && (x % 6 <= 1) && py >= 10 && py <= 52);
                bool front_serr = (x >= 88 && x <= 112 && (x % 6 <= 1) && py >= 16 && py <= 44);
                bool port = (x >= 52 && x <= 78 && py >= 32 && py <= 56);
                bool top_chamfer = (py >= 56 && py <= 60);
                if (port) {
                    bool brass_rim = (x >= 64 && x <= 72 && py >= 38 && py <= 50);
                    if (brass_rim) {
                        out[TEX_WEAPON].px[idx] = pack(220, 180, 68); /* brass cartridge casing */
                    } else {
                        int ch = 18 + (n & 5) + (py == 44 ? 14 : 0);
                        out[TEX_WEAPON].px[idx] = pack(ch, ch + 2, ch + 6);
                    }
                } else {
                    int sv = 186 + (py * 3) / 8 + grain - ((rear_serr || front_serr) ? 72 : 0) + (top_chamfer ? 44 : 0);
                    if (sv > 248) sv = 248;
                    if (sv < 42) sv = 42;
                    out[TEX_WEAPON].px[idx] = pack(sv, sv + 2, sv + 6);
                }
            } else if (x < 64) {
                /* Matte charcoal tactical glove leather & stippled polymer frame */
                bool stipple = ((x ^ y) & 1) != 0;
                bool rail = (y >= 48 && y <= 58 && (x % 10 < 4));
                bool seam = (y == 4 || y == 58 || x == 4 || x == 58);
                bool knuckle = (y >= 36 && y <= 42);
                int gv = 24 + (stipple ? 5 : 0) - (rail ? 8 : 0) - (seam ? 8 : 0)
                       + (knuckle ? 9 : 0) + (n & 3);
                out[TEX_WEAPON].px[idx] = pack(gv, gv + 2, gv + 5);
            } else if (x < 96) {
                out[TEX_WEAPON].px[idx] = camo_pixel_sz(x, y, 4);
            } else {
                int px = x - 96;
                bool baffle = (y % 12 <= 1);
                bool flute = (px % 8 < 2);
                int bv = 44 + (flute ? 10 : 0) - (baffle ? 10 : 0) + (n & 5);
                out[TEX_WEAPON].px[idx] = pack(bv, bv + 4, bv + 9);
            }

            /* 13. TEX_WATER: tileable midnight-indigo harbor waves & specular crests */
            {
                float fx = (float)x * (2.0f * PS15_PI / 128.0f);
                float fy = (float)y * (2.0f * PS15_PI / 128.0f);
                float w1 = sinf(fy * 3.0f + sinf(fx * 2.0f) * 1.4f);
                float w2 = cosf(fx * 2.0f - fy * 2.0f);
                float w3 = sinf(fx * 4.0f + fy * 5.0f);
                float w4 = sinf(fx * 7.0f - fy * 6.0f);   /* fine ripple octave */
                float wave = 0.48f + 0.28f * w1 + 0.15f * w2 + 0.06f * w3 + 0.03f * w4;
                int crest = (wave > 0.74f) ? (int)((wave - 0.74f) * 280.0f) : 0;
                int glitter = ((wave > 0.68f) && ((n & 7) == 0)) ? 42 : 0;
                int wr = 12 + (int)(wave * 18.0f) + crest + glitter;
                int wg = 22 + (int)(wave * 30.0f) + crest + glitter + (n & 3);
                int wb = 44 + (int)(wave * 48.0f) + crest + glitter + (n & 3);
                out[TEX_WATER].px[idx] = pack(wr, wg, wb);
            }

            /* 14. TEX_BEAM: smooth seamless volumetric cone falloff */
            {
                float u_rad = ((float)x / 128.0f) * 2.0f * PS15_PI;
                float lat = 0.68f + 0.32f * cosf(u_rad);
                float axial = (float)y / 127.0f;
                float val = axial * axial * lat;
                if (y < 24) val *= (float)y / 24.0f;
                int bv = (int)(val * 225.0f);
                out[TEX_BEAM].px[idx] = pack(bv, bv, bv);
            }

            /* 15. TEX_TERMINAL: ruggedized spy-city intelligence CRT terminal */
            {
                bool bezel = (x < 12 || x > 115 || y < 12 || y > 115);
                if (bezel) {
                    bool led_g = (x >= 96 && x <= 102 && y >= 4 && y <= 8);
                    bool led_a = (x >= 84 && x <= 90 && y >= 4 && y <= 8);
                    int mv = 32 + (n & 5);
                    out[TEX_TERMINAL].px[idx] = led_g ? pack(48, 228, 114)
                        : led_a ? pack(235, 164, 44)
                        : pack(mv, mv + 3, mv + 8);
                } else {
                    bool grid = ((x % 20 == 0) || (y % 20 == 0));
                    bool scan = (y & 1) == 0;
                    int dx = x - 64, dy = y - 64;
                    int r2 = dx * dx + dy * dy;
                    bool ring = (abs(r2 - 784) < 88) || (abs(r2 - 256) < 56);
                    bool text_bar = (y >= 84 && y <= 100 && (y % 6 != 0) &&
                                     hash2(x / 6, y / 6, 0xC0DEu) > 95);
                    int g = 42 + (scan ? 14 : 0) + (grid ? 32 : 0) +
                            (ring ? 105 : 0) + (text_bar ? 120 : 0);
                    int r = 12 + (ring ? 26 : 0) + (text_bar ? 40 : 0);
                    int b = 38 + (grid ? 24 : 0) + (ring ? 78 : 0) + (text_bar ? 88 : 0);
                    out[TEX_TERMINAL].px[idx] = pack(r, g, b);
                }
            }

            /* 16. TEX_INSIGNIA: Regime 3-bar banner, trooper respirator & Mecha */
            if (y >= 64) {
                int py = y - 64;
                bool border = (x <= 3 || x >= 124 || py <= 3 || py >= 60);
                bool bar = (py >= 10 && py <= 52) &&
                           ((x >= 36 && x <= 46) ||
                            (x >= 58 && x <= 68) ||
                            (x >= 80 && x <= 90));
                int weave = ((x + py) & 1) ? 3 : -3;
                int wear = hash2(x / 4, py / 4, 0x3BA2u) / 8;
                if (border) {
                    int bv = 28 + (n & 5);
                    out[TEX_INSIGNIA].px[idx] = pack(bv + 6, bv, bv + 2);
                } else if (bar) {
                    int wv = 248 - wear / 4 + weave;
                    out[TEX_INSIGNIA].px[idx] = pack(wv, wv - 2, wv - 4);
                } else {
                    int cr = 196 - wear + weave + (n & 5);
                    out[TEX_INSIGNIA].px[idx] = pack(cr, 24 + (n & 3), 30 + (n & 3));
                }
            } else if (x < 64) {
                int px = x;
                uint32_t c = pack(20 + (n & 5), 22 + (n & 5), 26 + (n & 5));
                if (y >= 34 && y <= 54 && px >= 6 && px <= 56) {
                    /* Dark tinted dual ballistic goggles with rubber frame & center bridge */
                    bool bridge = (px >= 28 && px <= 34);
                    bool frame = (y == 34 || y == 54 || px == 6 || px == 56 || bridge);
                    int gv = frame ? 34 : (10 + ((px + y) % 4 == 0 ? 18 : 0));
                    c = pack(gv, gv + 3, gv + 8);
                } else if (y >= 4 && y <= 32 && px >= 12 && px <= 50) {
                    /* Titanium respirator faceplate with 3 distinct vertical slits (|||) */
                    bool slit = (y >= 10 && y <= 28) &&
                                ((px >= 20 && px <= 22) ||
                                 (px >= 30 && px <= 32) ||
                                 (px >= 40 && px <= 42));
                    int bevel = (px == 12 || px == 50 || y == 4 || y == 32) ? -28 : 0;
                    int shade = abs(px - 31) * 2;
                    int mv = 195 - shade + bevel + (n & 5);
                    c = slit ? pack(10, 12, 16) : pack(mv, mv + 2, mv + 7);
                }
                out[TEX_INSIGNIA].px[idx] = c;
            } else {
                int px = x - 64;
                bool seam = (px <= 3 || px >= 60 || y <= 3 || y >= 60 || y == 30);
                bool eye = (y >= 46 && y <= 54 && px >= 12 && px <= 50);
                int chip = hash2(px, y, 0x9EC4u);
                if (eye) {
                    /* Horizontal glowing crimson optic sensor slit */
                    bool core = (y >= 48 && y <= 52 && px >= 16 && px <= 46);
                    out[TEX_INSIGNIA].px[idx] = core ? pack(255, 68, 54) : pack(218, 28, 32);
                } else if (seam || chip < 24) {
                    /* Dark cast-iron primer chipping & mechanical panel seams */
                    int dv = 42 + (n & 5);
                    out[TEX_INSIGNIA].px[idx] = pack(dv + 6, dv + 2, dv);
                } else {
                    /* Weathered bone-white ceramic armor plating */
                    int av = 216 - (chip < 68 ? 32 : 0) + (n & 7) - 3;
                    out[TEX_INSIGNIA].px[idx] = pack(av, av - 2, av - 5);
                }
            }
        }
    }
}

void texgen_free(Tex t[TEX_COUNT]) {
    for (int k = 0; k < TEX_COUNT; k++) { free(t[k].px); t[k].px = NULL; }
}
