#include "camera_gesture.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void){
    Fe8CameraGesture g={0};float dx,dy;
    fe8_camera_gesture_begin(&g,1,100,100,false,false);
    assert(fe8_camera_gesture_move(&g,103,102,&dx,&dy)==FE8_GESTURE_NONE);
    assert(fe8_camera_gesture_end(&g,1,103,102)==FE8_GESTURE_SELECT);
    fe8_camera_gesture_begin(&g,1,100,100,false,false);
    assert(fe8_camera_gesture_move(&g,107,100,&dx,&dy)==FE8_GESTURE_PAN&&dx==7&&dy==0);
    assert(fe8_camera_gesture_move(&g,103,105,&dx,&dy)==FE8_GESTURE_PAN&&dx==-4&&dy==5);
    assert(fe8_camera_gesture_end(&g,1,103,105)==FE8_GESTURE_NONE);
    fe8_camera_gesture_begin(&g,3,0,0,false,false);
    assert(fe8_camera_gesture_move(&g,0,6,&dx,&dy)==FE8_GESTURE_ORBIT);
    assert(fe8_camera_gesture_end(&g,3,0,6)==FE8_GESTURE_NONE);
    fe8_camera_gesture_begin(&g,3,0,0,false,false);
    assert(fe8_camera_gesture_end(&g,3,1,2)==FE8_GESTURE_CANCEL);
    fe8_camera_gesture_begin(&g,2,0,0,false,false);
    assert(fe8_camera_gesture_move(&g,10,0,&dx,&dy)==FE8_GESTURE_PAN);
    assert(fe8_camera_gesture_end(&g,2,10,0)==FE8_GESTURE_NONE);
    fe8_camera_gesture_begin(&g,1,0,0,false,false);
    assert(fe8_camera_gesture_end(&g,1,40,40)==FE8_GESTURE_NONE); /* coalesced motion */
    fe8_camera_gesture_begin(&g,1,0,0,true,false);
    assert(fe8_camera_gesture_move(&g,100,100,&dx,&dy)==FE8_GESTURE_NONE);
    assert(fe8_camera_gesture_end(&g,1,100,100)==FE8_GESTURE_NONE);
    fe8_camera_gesture_begin(&g,1,0,0,false,true);
    assert(fe8_camera_gesture_end(&g,1,0,0)==FE8_GESTURE_NONE);
    fe8_camera_gesture_begin(&g,1,0,0,false,false);
    assert(fe8_camera_gesture_end(&g,3,0,0)==FE8_GESTURE_NONE&&g.active);
    fe8_camera_gesture_reset(&g);assert(!g.active);
    assert(fe8_camera_gesture_end(&g,1,0,0)==FE8_GESTURE_NONE);
    fe8_camera_gesture_begin(&g,1,NAN,0,false,false);assert(!g.active);
    puts("PASS click/drag disambiguation, release-only A/B, UI capture, middle pan, right orbit, coalesced events, cancel and focus reset");
    return 0;
}
