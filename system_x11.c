#include "system.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <stdlib.h>
#include <stdint.h>

typedef struct {
    Window window;
    Display *display;
    int screen;
    GC gc;

    XImage *image;
    void *image_buffer;
    XImage *image_scaler;
    int image_scaler_width, image_scaler_height;
} X11Data;

static Atom delete_window_atom;

static void stretch_image(uint32_t *src_image, int src_x, int src_y, int src_width, int src_height, int src_pitch,
                          uint32_t *dst_image, int dst_x, int dst_y, int dst_width, int dst_height, int dst_pitch) {
    int x, y;
    int src_ox, src_oy;

    if (!src_image || !dst_image)
        return;

    src_image += src_x + src_y * src_pitch;
    dst_image += dst_x + dst_y * dst_pitch;

    const int delta_x = (src_width  << 16) / dst_width;
    const int delta_y = (src_height << 16) / dst_height;

    src_oy = 0;
    for (y = 0; y < dst_height; y++) {
        src_ox = 0;
        for (int x = 0; x < dst_width; x++) {
            dst_image[x] = src_image[src_ox >> 16];
            src_ox += delta_x;
        }

        src_oy += delta_y;
        if (src_oy >= 0x10000) {
            src_image += (src_oy >> 16) * src_pitch;
            src_oy &= 0xffff;
        }

        dst_image += dst_pitch;
    }
}

static void process_event(SysWindow *window, XEvent *event) {
    switch (event->type) {
    case ConfigureNotify:
        window->width = event->xconfigure.width;
        window->height = event->xconfigure.height;
        window->dst_ox = 0;
        window->dst_oy = 0;
        window->dst_width = window->width;
        window->dst_height = window->height;

        X11Data *data = (X11Data*) window->data;
        if (data->image_scaler) {
                data->image_scaler->data = NULL;
                XDestroyImage(data->image_scaler);
                data->image_scaler = NULL;
                data->image_scaler_width = 0;
                data->image_scaler_height = 0;
        }
        XClearWindow(data->display, data->window);
        break;

    case ClientMessage:
        if ((Atom) event->xclient.data.l[0] == delete_window_atom) {
            window->close = true;
        }
        break;

    case DestroyNotify:
        window->close = true;
        break;
    }
}

static void destroy_window(SysWindow *window) {
    X11Data *data = (X11Data*) window->data;

    if (data->image) {
        data->image->data = NULL;
        XDestroyImage(data->image);
        XDestroyWindow(data->display, data->window);
        XCloseDisplay(data->display);
    }
}

SysWindow* sys_open(const char* title, int width, int height) {
    SysWindow *window = calloc(1, sizeof(SysWindow));
    if (!window) { return NULL; }

    X11Data *data = calloc(1, sizeof(X11Data));
    if (!data) { free(window); return NULL; }

    window->data = data;

    data->display = XOpenDisplay(NULL);
    if (!data->display) {
        free(data);
        free(window);
        return NULL;
    }

    data->screen = DefaultScreen(data->display);

    int format_count;

    Visual *visual = DefaultVisual(data->display, data->screen);
    XPixmapFormatValues *formats = XListPixmapFormats(data->display, &format_count);
    int depth = DefaultDepth(data->display, data->screen);

    Window root = DefaultRootWindow(data->display);

    int conv_depth = 0;

    for (int i = 0; i < format_count; i++) {
        if (depth == formats[i].depth) {
            conv_depth = formats[i].bits_per_pixel;
            break;
        }
    }

    XFree(formats);

    if (conv_depth != 32) {
        XCloseDisplay(data->display);
        return NULL;
    }

    int screen_width = DisplayWidth(data->display, data->screen);
    int screen_height = DisplayHeight(data->display, data->screen);

    XSetWindowAttributes wa;
    wa.border_pixel = BlackPixel(data->display, data->screen);
    wa.background_pixel = BlackPixel(data->display, data->screen);
    wa.backing_store = NotUseful;

    window->width = width;
    window->height = height;
    window->buffer_width = width;
    window->buffer_height = height;
    window->dst_width = width;
    window->dst_height = height;

    int pos_x = (screen_width - width) / 2;
    int pos_y = (screen_height - height) / 2;

    data->window = XCreateWindow(data->display, root, pos_x, pos_y, width, height, 0, depth, InputOutput, visual, CWBackPixel | CWBorderPixel | CWBackingStore, &wa);
    if (!data->window) {
        return NULL;
    }

    XSelectInput(data->display, data->window,
        KeyPressMask | KeyReleaseMask
        | ButtonPressMask | ButtonReleaseMask | PointerMotionMask
        | StructureNotifyMask | ExposureMask
        | FocusChangeMask
        | EnterWindowMask | LeaveWindowMask
    );

    XStoreName(data->display, data->window, title);

    XSizeHints sh;
    sh.flags = PPosition | PMinSize | PMaxSize;
    sh.x = 0;
    sh.y = 0;
    sh.min_width = width;
    sh.min_height = height;
    sh.max_width = screen_width;
    sh.max_height = screen_height;

    delete_window_atom = XInternAtom(data->display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(data->display, data->window, &delete_window_atom, 1);

    XSetWMNormalHints(data->display, data->window, &sh);
    XClearWindow(data->display, data->window);
    XMapRaised(data->display, data->window);
    XFlush(data->display);

    data->gc = DefaultGC(data->display, data->screen);
    data->image = XCreateImage(data->display, CopyFromParent, depth, ZPixmap, 0, NULL, width, height, 32, width * 4);

    return window;
}

int sys_update(SysWindow* window, void *buffer, int width, int height) {
    if (!window) {
        return 1;
    }

    X11Data *data = (X11Data*) window->data;

    if (window->close) {
        destroy_window(window);
        return 1;
    }

    if (!buffer) {
        return 1;
    }

    bool different_size = false;

    if (window->buffer_width != width || window->buffer_height != height) {
        window->buffer_width = width;
        window->buffer_height = height;
        different_size = true;
    }

    if (different_size || window->buffer_width != window->dst_width || window->buffer_height != window->dst_height) {
        if (data->image_scaler_width != window->dst_width || data->image_scaler_height != window->dst_height) {
            if (data->image_scaler) {
                data->image_scaler->data = NULL;
                XDestroyImage(data->image_scaler);
            }

            if (data->image_buffer) {
                free(data->image_buffer);
                data->image_buffer = NULL;
            }

            int depth = DefaultDepth(data->display, data->screen);
            data->image_buffer = malloc(window->dst_width * window->dst_height * 4);

            if (!data->image_buffer) {
                return 1;
            }

            data->image_scaler_width  = window->dst_width;
            data->image_scaler_height = window->dst_height;
            data->image_scaler = XCreateImage(data->display, CopyFromParent, depth, ZPixmap, 0, NULL, data->image_scaler_width, data->image_scaler_height, 32, data->image_scaler_width * 4);
        }
    }

    if (data->image_scaler) {
        stretch_image((uint32_t*) buffer, 0, 0, window->buffer_width, window->buffer_height, window->buffer_width,
                      (uint32_t*) data->image_buffer, 0, 0, window->dst_width, window->dst_height, window->dst_width);
        data->image_scaler->data = (char*) data->image_buffer;
        XPutImage(data->display, data->window, data->gc, data->image_scaler, 0, 0, window->dst_ox, window->dst_oy, window->dst_width, window->dst_height);
    } else {
        data->image->data = (char*) buffer;
        XPutImage(data->display, data->window, data->gc, data->image, 0, 0, window->dst_ox, window->dst_oy, window->dst_width, window->dst_height);
    }

    XFlush(data->display);

    XEvent event;
    while (!window->close && XPending(data->display)) {
        XNextEvent(data->display, &event);
        process_event(window, &event);
    }

    return 0;
}
