/* X11 platform: window, XImage presentation, keyboard + pointer.
 * Mouse look uses motion deltas from the window centre, re-warping
 * the pointer so the view can turn indefinitely. */
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
    uint32_t prev_keys;
    int warp_guard;
    Atom wm_delete;
};

static uint32_t key_bit(KeySym ks) {
    switch (ks) {
        case XK_w: case XK_W: return K_FWD;
        case XK_s: case XK_S: return K_BACK;
        case XK_a: case XK_A: return K_LEFT;
        case XK_d: case XK_D: return K_RIGHT;
        case XK_Shift_L: case XK_Shift_R: return K_SPRINT;
        case XK_Control_L: case XK_Control_R: return K_CROUCH;
        case XK_space: return K_JUMP;
        case XK_e: case XK_E: return K_GRAPPLE;
        case XK_v: case XK_V: return K_TOGGLE_VIEW;
        case XK_Escape: return K_QUIT;
        default: return 0;
    }
}

Plat *plat_open(const char *title, int w, int h) {
    Plat *p = calloc(1, sizeof *p);
    p->w = w; p->h = h;
    p->dpy = XOpenDisplay(NULL);
    if (!p->dpy) { free(p); return NULL; }
    int screen = DefaultScreen(p->dpy);
    p->win = XCreateSimpleWindow(p->dpy, RootWindow(p->dpy, screen), 10, 10,
                                 (unsigned)w, (unsigned)h, 1,
                                 BlackPixel(p->dpy, screen),
                                 BlackPixel(p->dpy, screen));
    XStoreName(p->dpy, p->win, title);
    XSelectInput(p->dpy, p->win, KeyPressMask | KeyReleaseMask |
                 PointerMotionMask | ButtonPressMask | ExposureMask |
                 StructureNotifyMask);
    p->wm_delete = XInternAtom(p->dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(p->dpy, p->win, &p->wm_delete, 1);
    p->gc = XCreateGC(p->dpy, p->win, 0, NULL);
    p->img = XCreateImage(p->dpy, DefaultVisual(p->dpy, screen), 24, ZPixmap,
                          0, malloc((size_t)w * h * 4), (unsigned)w,
                          (unsigned)h, 32, 0);
    XMapWindow(p->dpy, p->win);
    XFlush(p->dpy);
    return p;
}

int plat_poll(Plat *p, Input *in) {
    in->keys = 0;
    in->mouse_dx = 0;
    in->mouse_dy = 0;
    static uint32_t held;
    int quit = 0;
    while (XPending(p->dpy) > 0) {
        XEvent ev;
        XNextEvent(p->dpy, &ev);
        if (ev.type == KeyPress || ev.type == KeyRelease) {
            uint32_t bit = key_bit(XLookupKeysym(&ev.xkey, 0));
            if (ev.type == KeyPress) held |= bit; else held &= ~bit;
            if (bit == K_QUIT && ev.type == KeyPress) quit = 1;
        } else if (ev.type == MotionNotify) {
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
    in->keys = held;
    in->pressed = held & ~p->prev_keys;
    p->prev_keys = held;
    return quit ? 0 : 1;
}

void plat_present(Plat *p, const uint32_t *px) {
    memcpy(p->img->data, px, (size_t)p->w * p->h * 4);
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
