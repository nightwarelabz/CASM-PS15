/* X11 platform: window, XImage presentation, keyboard + pointer capture. */
#include "platform.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdlib.h>
#include <string.h>

struct Plat {
    Display *dpy;
    Window win;
    GC gc;
    XImage *img;
    int w, h;
    uint32_t held;
    uint32_t prev_keys;
    int warp_guard;
    int mouse_locked;
    Atom wm_delete;
};

static uint32_t key_bit(KeySym ks) {
    switch (ks) {
        case XK_w: case XK_W: return K_FWD;
        case XK_s: case XK_S: return K_BACK;
        case XK_a: case XK_A: return K_LEFT;
        case XK_d: case XK_D: return K_RIGHT;
        case XK_c: case XK_C: return K_CROUCH;
        case XK_Shift_L: case XK_Shift_R: return K_SPRINT;
        case XK_Control_L: case XK_Control_R: return K_CROUCH;
        case XK_space: return K_JUMP;
        case XK_e: case XK_E: return K_GRAPPLE;
        case XK_v: case XK_V: return K_TOGGLE_VIEW;
        case XK_Escape: return K_QUIT;
        case XK_1: case XK_F1: return K_HUD;
        case XK_2: case XK_F2: return K_SNAP_MODE;
        case XK_3: case XK_F3: return K_UV_MODE;
        case XK_4: case XK_F4: return K_DAC_MODE;
        case XK_5: case XK_F5: return K_WIRE_MODE;
        case XK_n: case XK_N: case XK_6: case XK_F6: return K_VISOR;
        case XK_7: case XK_F7: return K_CRT;
        case XK_m: case XK_M: case XK_8: return K_DOSSIER;
        case XK_q: case XK_Q: return K_AIM;
        case XK_r: case XK_R: return K_USE;
        case XK_f: case XK_F: return K_MELEE;
        case XK_x: case XK_X: case XK_Return: return K_FIRE;
        case XK_Left:  return K_LOOK_L;
        case XK_Right: return K_LOOK_R;
        case XK_Up:    return K_LOOK_U;
        case XK_Down:  return K_LOOK_D;
        default: return 0;
    }
}

Plat *plat_open(const char *title, int w, int h) {
    Plat *p = calloc(1, sizeof *p);
    p->w = w; p->h = h;
    p->mouse_locked = 1;
    p->dpy = XOpenDisplay(NULL);
    if (!p->dpy) { free(p); return NULL; }
    int screen = DefaultScreen(p->dpy);
    p->win = XCreateSimpleWindow(p->dpy, RootWindow(p->dpy, screen), 10, 10,
                                 (unsigned)w, (unsigned)h, 1,
                                 BlackPixel(p->dpy, screen),
                                 BlackPixel(p->dpy, screen));
    XStoreName(p->dpy, p->win, title);
    XSelectInput(p->dpy, p->win, KeyPressMask | KeyReleaseMask |
                 PointerMotionMask | ButtonPressMask | ButtonReleaseMask |
                 ExposureMask | StructureNotifyMask);
    p->wm_delete = XInternAtom(p->dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(p->dpy, p->win, &p->wm_delete, 1);
    p->gc = XCreateGC(p->dpy, p->win, 0, NULL);
    p->img = XCreateImage(p->dpy, DefaultVisual(p->dpy, screen), 24, ZPixmap,
                          0, malloc((size_t)w * (size_t)h * 4), (unsigned)w,
                          (unsigned)h, 32, 0);
    XMapWindow(p->dpy, p->win);
    XFlush(p->dpy);
    return p;
}

int plat_poll(Plat *p, Input *in) {
    in->keys = 0;
    in->mouse_dx = 0;
    in->mouse_dy = 0;
    int quit = 0;
    while (XPending(p->dpy) > 0) {
        XEvent ev;
        XNextEvent(p->dpy, &ev);
        if (ev.type == KeyPress || ev.type == KeyRelease) {
            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            if (ks == XK_Tab && ev.type == KeyPress) {
                p->mouse_locked = !p->mouse_locked;
                continue;
            }
            uint32_t bit = key_bit(ks);
            if (ev.type == KeyPress) p->held |= bit;
            else p->held &= ~bit;
            if (bit == K_QUIT && ev.type == KeyPress) quit = 1;
        } else if (ev.type == ButtonPress) {
            if (!p->mouse_locked) {
                p->mouse_locked = 1;
            } else if (ev.xbutton.button == Button1) {
                p->held |= K_FIRE;
            } else if (ev.xbutton.button == Button3) {
                p->held |= K_AIM;
            }
        } else if (ev.type == ButtonRelease) {
            if (ev.xbutton.button == Button1) p->held &= ~K_FIRE;
            if (ev.xbutton.button == Button3) p->held &= ~K_AIM;
        } else if (ev.type == MotionNotify && p->mouse_locked) {
            if (p->warp_guard > 0) { p->warp_guard--; continue; }
            int dx = ev.xmotion.x - p->w / 2;
            int dy = ev.xmotion.y - p->h / 2;
            if (dx || dy) {
                in->mouse_dx += (float)dx;
                in->mouse_dy += (float)dy;
                XWarpPointer(p->dpy, None, p->win, 0, 0, 0, 0, p->w / 2, p->h / 2);
                p->warp_guard++;
                XFlush(p->dpy);
            }
        } else if (ev.type == ClientMessage) {
            if ((Atom)ev.xclient.data.l[0] == p->wm_delete) quit = 1;
        } else if (ev.type == DestroyNotify) {
            quit = 1;
        }
    }
    in->keys = p->held;
    in->pressed = p->held & ~p->prev_keys;
    p->prev_keys = p->held;
    return quit ? 0 : 1;
}

void plat_present(Plat *p, const uint32_t *px) {
    memcpy(p->img->data, px, (size_t)p->w * (size_t)p->h * 4);
    XPutImage(p->dpy, p->win, p->gc, p->img, 0, 0, 0, 0,
              (unsigned)p->w, (unsigned)p->h);
    XFlush(p->dpy);
}

void plat_close(Plat *p) {
    if (!p) return;
    XDestroyImage(p->img);
    XFreeGC(p->dpy, p->gc);
    XDestroyWindow(p->dpy, p->win);
    XCloseDisplay(p->dpy);
    free(p);
}
