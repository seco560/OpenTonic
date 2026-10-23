/* Free-fly camera in engine space (Z up). Port of Sources/TonicViewer/Camera.swift.
 * Port control defaults: WASD moves, arrow keys turn the camera (replacing the original's numpad camera). */
#ifndef TV_CAMERA_H
#define TV_CAMERA_H
#include <tonic/tonic.h>

typedef struct camera {
    tc_vec3 position;
    float yaw;           /* radians, 0 = looking along +Y */
    float pitch;         /* radians, positive = up */
    /* Vertical field of view. Widescreen gets a wider horizontal view (Hor+), never a stretched image. */
    float vertical_fov;
    float near_z, far_z;
} camera;

void camera_init(camera *c);
tc_vec3 camera_forward(const camera *c);
tc_vec3 camera_right(const camera *c);
/* Right-handed view, perspective with 0..1 clip-space depth (SDL_GPU, like Metal). Column-major. */
tc_mat4 camera_view_projection(const camera *c, float aspect);

#endif
