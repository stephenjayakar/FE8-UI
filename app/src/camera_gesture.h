#ifndef FE8_CAMERA_GESTURE_H
#define FE8_CAMERA_GESTURE_H
#include <stdbool.h>
/* Window points (not Retina pixels). Dragging never generates A or B. */
typedef enum Fe8CameraGestureAction {
    FE8_GESTURE_NONE, FE8_GESTURE_PAN, FE8_GESTURE_ORBIT,
    FE8_GESTURE_SELECT, FE8_GESTURE_CANCEL
} Fe8CameraGestureAction;
typedef struct Fe8CameraGesture {
    bool active, dragging, ui, suppress_click;
    int button;
    float start_x,start_y,last_x,last_y;
} Fe8CameraGesture;
void fe8_camera_gesture_reset(Fe8CameraGesture *g);
void fe8_camera_gesture_begin(Fe8CameraGesture *g,int button,float x,float y,
    bool over_ui,bool suppress_click);
Fe8CameraGestureAction fe8_camera_gesture_move(Fe8CameraGesture *g,float x,float y,float *dx,float *dy);
Fe8CameraGestureAction fe8_camera_gesture_end(Fe8CameraGesture *g,int button,float x,float y);
#endif
