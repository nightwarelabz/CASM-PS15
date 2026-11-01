/* Cocoa platform (Objective-C): window, CGImage presentation, keyboard +
 * click-to-capture mouse look (Tab releases/toggles cursor). */
#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#include "platform.h"
#include <stdlib.h>
#include <string.h>

@interface CASMView : NSView
@property (nonatomic) CGImageRef image;
@property (nonatomic) uint32_t keyBits;
@property (nonatomic) uint32_t modBits;
@property (nonatomic) uint32_t mouseBits;
@property (nonatomic) float mdx, mdy;
@property (nonatomic) BOOL quit;
@property (nonatomic) BOOL mouseCaptured;
- (void)setCapture:(BOOL)cap;
@end

@implementation CASMView
- (BOOL)acceptsFirstResponder { return YES; }

- (void)setCapture:(BOOL)cap {
    if (self.mouseCaptured == cap) return;
    self.mouseCaptured = cap;
    CGAssociateMouseAndMouseCursorPosition(cap ? false : true);
    if (cap) [NSCursor hide];
    else     [NSCursor unhide];
}

- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    if (!self.image) return;
    CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
    CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);
    CGRect r = CGRectMake(0, 0, self.bounds.size.width, self.bounds.size.height);
    CGContextDrawImage(ctx, r, self.image);
}

- (uint32_t)bitForCode:(unsigned short)code {
    switch (code) {
        case 13:  return K_FWD;          /* W */
        case 1:   return K_BACK;         /* S */
        case 0:   return K_LEFT;         /* A */
        case 2:   return K_RIGHT;        /* D */
        case 8:   return K_CROUCH;       /* C (ergonomic crouch alternative) */
        case 49:  return K_JUMP;         /* Space */
        case 14:  return K_GRAPPLE;      /* E */
        case 9:   return K_TOGGLE_VIEW;  /* V */
        case 53:  return K_QUIT;         /* Escape */
        case 18: case 122: return K_HUD;       /* 1 or F1 */
        case 19: case 120: return K_SNAP_MODE; /* 2 or F2 */
        case 20: case 99:  return K_UV_MODE;   /* 3 or F3 */
        case 21: case 118: return K_DAC_MODE;  /* 4 or F4 */
        case 23: case 96:  return K_WIRE_MODE; /* 5 or F5 */
        case 45: case 22: case 97: return K_VISOR;   /* N, 6 or F6 */
        case 26: case 98:  return K_CRT;       /* 7 or F7 */
        case 46: case 28:  return K_DOSSIER;   /* M or 8 */
        case 12:  return K_AIM;          /* Q (aim alternative to RMB) */
        case 15:  return K_USE;          /* R (interact / reload) */
        case 3:   return K_MELEE;        /* F (silent CQC takedown) */
        case 7: case 36: return K_FIRE;  /* X or Return (fire alternative to LMB) */
        case 123: return K_LOOK_L;       /* Left Arrow */
        case 124: return K_LOOK_R;       /* Right Arrow */
        case 126: return K_LOOK_U;       /* Up Arrow */
        case 125: return K_LOOK_D;       /* Down Arrow */
        default:  return 0;
    }
}

- (void)keyDown:(NSEvent *)e {
    if (e.keyCode == 48) { /* Tab toggles mouse capture */
        if (!e.isARepeat) [self setCapture:!self.mouseCaptured];
        return;
    }
    uint32_t b = [self bitForCode:e.keyCode];
    if (b == K_QUIT) {
        [self setCapture:NO];
        self.quit = YES;
    }
    self.keyBits |= b;
}

- (void)keyUp:(NSEvent *)e {
    self.keyBits &= ~[self bitForCode:e.keyCode];
}

- (void)flagsChanged:(NSEvent *)e {
    NSUInteger f = e.modifierFlags;
    uint32_t m = 0;
    if (f & NSEventModifierFlagShift)   m |= K_SPRINT;
    if (f & NSEventModifierFlagControl) m |= K_CROUCH;
    self.modBits = m;
}

- (void)mouseDown:(NSEvent *)e {
    (void)e;
    if (!self.mouseCaptured) {
        [self setCapture:YES];
    } else {
        self.mouseBits |= K_FIRE;
    }
}

- (void)mouseUp:(NSEvent *)e {
    (void)e;
    self.mouseBits &= ~K_FIRE;
}

- (void)rightMouseDown:(NSEvent *)e {
    (void)e;
    if (!self.mouseCaptured) [self setCapture:YES];
    self.mouseBits |= K_AIM;
}

- (void)rightMouseUp:(NSEvent *)e {
    (void)e;
    self.mouseBits &= ~K_AIM;
}

- (void)accumMouse:(NSEvent *)e {
    self.mdx += (float)e.deltaX;
    self.mdy += (float)e.deltaY;
}
- (void)mouseMoved:(NSEvent *)e        { [self accumMouse:e]; }
- (void)mouseDragged:(NSEvent *)e      { [self accumMouse:e]; }
- (void)rightMouseDragged:(NSEvent *)e { [self accumMouse:e]; }
@end

struct Plat {
    NSWindow *win;
    CASMView *view;
    CGColorSpaceRef cs;
    uint32_t prev_keys;
    unsigned char *rgba;
    int w, h;
};

Plat *plat_open(const char *title, int w, int h) {
    Plat *p = calloc(1, sizeof *p);
    p->w = w; p->h = h;
    p->rgba = malloc((size_t)w * (size_t)h * 4);
    p->cs = CGColorSpaceCreateDeviceRGB();
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    NSRect frame = NSMakeRect(0, 0, w, h);
    p->win = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                             NSWindowStyleMaskMiniaturizable)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    p->win.title = [NSString stringWithUTF8String:title];
    p->view = [[CASMView alloc] initWithFrame:frame];
    p->win.contentView = p->view;
    p->win.acceptsMouseMovedEvents = YES;
    [p->win makeFirstResponder:p->view];
    [p->win center];
    [p->win makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    return p;
}

int plat_poll(Plat *p, Input *in) {
    NSEvent *e;
    while ((e = [NSApp nextEventMatchingMask:NSEventMaskAny
                                    untilDate:[NSDate distantPast]
                                       inMode:NSDefaultRunLoopMode
                                      dequeue:YES])) {
        [NSApp sendEvent:e];
    }
    if (!p->win.isKeyWindow && p->view.mouseCaptured) {
        [p->view setCapture:NO];
        p->view.mouseBits = 0;
    }
    if (p->view.quit || !p->win.isVisible) {
        [p->view setCapture:NO];
        return 0;
    }
    uint32_t held = p->view.keyBits | p->view.modBits | p->view.mouseBits;
    in->keys = held;
    in->pressed = held & ~p->prev_keys;
    p->prev_keys = held;
    in->mouse_dx = p->view.mdx;
    in->mouse_dy = p->view.mdy;
    p->view.mdx = 0;
    p->view.mdy = 0;
    return 1;
}

void plat_present(Plat *p, const uint32_t *px) {
    int n = p->w * p->h;
    unsigned char *dst = p->rgba;
    for (int i = 0; i < n; i++) {
        uint32_t c = px[i];
        dst[i * 4 + 0] = (unsigned char)(c >> 16);
        dst[i * 4 + 1] = (unsigned char)(c >> 8);
        dst[i * 4 + 2] = (unsigned char)(c);
        dst[i * 4 + 3] = 255;
    }
    CGDataProviderRef prov = CGDataProviderCreateWithData(
        NULL, p->rgba, (size_t)n * 4, NULL);
    CGImageRef img = CGImageCreate(
        (size_t)p->w, (size_t)p->h, 8, 32, (size_t)p->w * 4, p->cs,
        kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big,
        prov, NULL, false, kCGRenderingIntentDefault);
    CGDataProviderRelease(prov);
    if (p->view.image) CGImageRelease(p->view.image);
    p->view.image = img;
    [p->view setNeedsDisplay:YES];
    [p->view displayIfNeeded];
}

void plat_close(Plat *p) {
    if (!p) return;
    [p->view setCapture:NO];
    if (p->view.image) CGImageRelease(p->view.image);
    if (p->cs) CGColorSpaceRelease(p->cs);
    [p->win close];
    free(p->rgba);
    free(p);
}
