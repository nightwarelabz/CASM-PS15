#ifndef CASM_GREYPORT_H
#define CASM_GREYPORT_H

#include "engine.h"

#define GREYPORT_MAX_AABBS   20
#define GREYPORT_MAX_LIGHTS   8
#define GREYPORT_MAX_SPOTS    6
#define GREYPORT_NUM_LAMPS    3
#define GREYPORT_NUM_TERMS    2
#define GREYPORT_WATER_Y     (-0.52f)

enum {
    GP_HIT_NONE = 0,
    GP_HIT_WALL = 1,
    GP_HIT_LAMP = 2,
    GP_HIT_GUARD_BODY = 3,
    GP_HIT_GUARD_HEAD = 4,
    GP_HIT_MECHA_ARMOR = 5,
    GP_HIT_MECHA_SENSOR = 6,
    GP_HIT_MECHA_ACTUATOR = 7,
    GP_HIT_MECHA_COOLANT = 8
};

typedef struct {
    Tex tex[TEX_COUNT];
    Tex brick_file;
    int have_brick_file;
    int have_crate;

    /* World, harbor canal & architectural meshes */
    Mesh ground, barge_deck, seawall_e, seawall_w, canal_water;
    Mesh canal_rail, viaduct, viaduct_rail;
    Mesh ware1, ware1_plinth, ware1_roof, ware1_door, ware1_win, ware1_rail;
    Mesh ware1_belt, pilaster, sidewalk_w, sidewalk_e, lamp_bulb, bollard;
    Mesh ware2, ware2_base, ware2_roof, ware2_win;
    Mesh banner_a, banner_b;
    Mesh crane_mast, crane_jib;
    Mesh catwalk, catwalk_rail;
    Mesh tower_shaft, tower_cab, tower_win, tower_roof, tower_rail;
    Mesh container_a, container_b, container_c, crate;
    Mesh pole, lamp_arm, lamp_head;
    Mesh term_base, term_screen;
    Mesh skyline_a, skyline_b, skyline_c;
    Mesh beam_cone, guard_cone, shadow_quad, puddle_quad, claw;

    /* Physics colliders aligned 1:1 with rendered geometry */
    AABB world[GREYPORT_MAX_AABBS];
    int  nworld;

    /* Grapple anchor targets in the yard */
    V3   anchors[7];
    int  nanchors;

    /* Interactive shootable sodium lamps & hackable intel terminals */
    V3   lamp_pos[GREYPORT_NUM_LAMPS];
    bool lamp_out[GREYPORT_NUM_LAMPS];
    V3   term_pos[GREYPORT_NUM_TERMS];
    bool term_hacked[GREYPORT_NUM_TERMS];

    /* Transient weapon muzzle-flash point light */
    V3    muzzle_pos;
    float muzzle_flash;

    /* Dynamic lighting state updated per frame */
    PointLight lights[GREYPORT_MAX_LIGHTS];
    SpotLight  spots[GREYPORT_MAX_SPOTS];
    LightRig   rig;
} GreyportScene;

void greyport_init(GreyportScene *sc);
void greyport_free(GreyportScene *sc);

void greyport_update_lights(GreyportScene *sc, const Guard *guards, int nguards, float time_s);
void greyport_render_opaque(FB *fb, const GreyportScene *sc, const Camera *cam, float time_s);
void greyport_render_fx(FB *fb, const GreyportScene *sc, const Camera *cam,
                        const Guard *guards, int nguards, float time_s);
void greyport_render_grapple(FB *fb, const GreyportScene *sc, const Camera *cam,
                             V3 hand, V3 anchor);

/* Hitscan pistol shot against world AABBs, shootable lamps, guards & Mecha. */
int greyport_fire_ray(GreyportScene *sc, V3 orig, V3 dir,
                      Guard *guards, int nguards, V3 *out_hit);
int greyport_fire_ray_ex(GreyportScene *sc, V3 orig, V3 dir,
                         Guard *guards, int nguards, Mecha *mecha, V3 *out_hit);

#endif
