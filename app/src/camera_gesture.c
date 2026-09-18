#include "camera_gesture.h"
#include <math.h>
#include <string.h>
void fe8_camera_gesture_reset(Fe8CameraGesture *g){if(g)memset(g,0,sizeof(*g));}
void fe8_camera_gesture_begin(Fe8CameraGesture *g,int button,float x,float y,bool ui,bool suppress) {
    if(!g||g->active||button<1||button>3||!isfinite(x)||!isfinite(y))return;
    *g=(Fe8CameraGesture){.active=true,.button=button,.start_x=x,.start_y=y,
        .last_x=x,.last_y=y,.ui=ui,.suppress_click=suppress};
}
Fe8CameraGestureAction fe8_camera_gesture_move(Fe8CameraGesture *g,float x,float y,float *dx,float *dy) {
    if(dx)*dx=0;
    if(dy)*dy=0;
    if(!g||!g->active||!isfinite(x)||!isfinite(y))return FE8_GESTURE_NONE;
    float ax=x-g->start_x,ay=y-g->start_y;
    if(!g->dragging && ax*ax+ay*ay>=36)g->dragging=true;
    if(!g->dragging)return FE8_GESTURE_NONE;
    if(dx)*dx=x-g->last_x;
    if(dy)*dy=y-g->last_y;
    g->last_x=x;g->last_y=y;
    if(g->ui)return FE8_GESTURE_NONE; /* Dragging a menu never confirms it. */
    return g->button==3?FE8_GESTURE_ORBIT:FE8_GESTURE_PAN;
}
Fe8CameraGestureAction fe8_camera_gesture_end(Fe8CameraGesture *g,int button,float x,float y) {
    if(!g||!g->active||g->button!=button)return FE8_GESTURE_NONE;
    /* A coalesced/missing motion event must not turn a distant release into A. */
    (void)fe8_camera_gesture_move(g,x,y,NULL,NULL);
    bool click=!g->dragging&&!g->suppress_click&&isfinite(x)&&isfinite(y);
    fe8_camera_gesture_reset(g);
    return !click?FE8_GESTURE_NONE:button==1?FE8_GESTURE_SELECT:
        button==3?FE8_GESTURE_CANCEL:FE8_GESTURE_NONE;
}
