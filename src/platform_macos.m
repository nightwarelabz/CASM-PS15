/* Cocoa platform (Objective-C): window, CGImage presentation,
 * keyboard + mouse. The host drives a manual event pump — plat_poll
 * drains the event queue each frame — so no app bundle or run loop
 * is required. */
#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#include "platform.h"
#include <stdlib.h>
#include <string.h>

@interface CASMView : NSView
@property (nonatomic) CGImageRef image;
@property (nonatomic) uint32_t held;
@property (nonatomic) float mdx, mdy;
@property (nonatomic) BOOL quit;
@end

@implementation CASMView
- (BOOL)acceptsFirstResponder { return YES; }
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    if (!self.image) return;
    CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
    CGRect r = CGRectMake(0, 0, self.bounds.size.width, self.bounds.size.height);
    CGContextDrawImage(ctx, r, self.image);
}
- (uint32_t)bitForCode:(unsigned short)code {
    switch (code) {
        case 13: return K_FWD;          /* W */
        case 1:  return K_BACK;         /* S */
        case 0:  return K_LEFT;         /* A */
        case 2:  return K_RIGHT;        /* D */
        case 49: return K_JUMP;         /* Space */
        case 14: return K_GRAPPLE;      /* E */
        case 9:  return K_TOGGLE_VIEW;  /* V */
        case 53: return K_QUIT;         /* Escape */
        default: return 0;
    }
}
- (void)keyDown:(NSEvent *)e {
    uint32_t b = [self bitForCode:e.keyCode];
    if (b == K_QUIT) self.quit = YES;
    self.held |= b;
}
- (void)keyUp:(NSEvent *)e {
    self.held &= ~[self bitForCode:e.keyCode];
}
- (void)flagsChanged:(NSEvent *)e {
    NSUInteger f = e.modifierFlags;
    if (f & NSEventModifierFlagShift) self.held |= K_SPRINT;
    else self.held &= ~K_SPRINT;
    if (f & NSEventModifierFlagControl) self.held |= K_CROUCH;
    else self.held &= ~K_CROUCH;
}
- (void)mouseMoved:(NSEvent *)e {
    self.mdx += (float)e.deltaX;
    self.mdy += (float)e.deltaY;
}
@end

struct Plat {
    NSWindow *win;
    CASMView *view;
    uint32_t prev_keys;
    unsigned char *rgba;    /* 960x540x4 conversion buffer */
    int w, h;
};

Plat *plat_open(const char *title, int w, int h) {
    Plat *p = calloc(1, sizeof *p);
    p->w = w; p->h = h;
    p->rgba = malloc((size_t)w * h * 4);
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
    if (p->view.quit || !p->win.isVisible) return 0;
    in->keys = p->view.held;
    in->pressed = p->view.held & ~p->prev_keys;
    p->prev_keys = p->view.held;
    in->mouse_dx = p->view.mdx;
    in->mouse_dy = p->view.mdy;
    p->view.mdx = 0;
    p->view.mdy = 0;
    return 1;
}

void plat_present(Plat *p, const uint32_t *px) {
    int n = p->w * p->h;
    for (int i = 0; i < n; i++) {
        p->rgba[i * 4 + 0] = (unsigned char)(px[i] >> 16);
        p->rgba[i * 4 + 1] = (unsigned char)(px[i] >> 8);
        p->rgba[i * 4 + 2] = (unsigned char)(px[i]);
        p->rgba[i * 4 + 3] = 255;
    }
    CGDataProviderRef prov = CGDataProviderCreateWithData(
        NULL, p->rgba, (size_t)n * 4, NULL);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGImageRef img = CGImageCreate(
        p->w, p->h, 8, 32, (size_t)p->w * 4, cs,
        kCGImageAlphaLast | kCGBitmapByteOrder32Big,
        prov, NULL, false, kCGRenderingIntentDefault);
    CGColorSpaceRelease(cs);
    CGDataProviderRelease(prov);
    if (p->view.image) CGImageRelease(p->view.image);
    p->view.image = img;
    [p->view setNeedsDisplay:YES];
    [p->view displayIfNeeded];
}

void plat_close(Plat *p) {
    if (!p) return;
    if (p->view.image) CGImageRelease(p->view.image);
    [p->win close];
    free(p->rgba);
    free(p);
}
