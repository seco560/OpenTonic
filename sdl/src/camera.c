#include "camera.h"

void camera_init(camera *c) {
    c->position = tc_v3(0, 0, 0);
    c->yaw = 0;
    c->pitch = 0;
    c->vertical_fov = 55 * TC_PI / 180;
    c->near_z = 0.05f;
    c->far_z = 2000;
}

tc_vec3 camera_forward(const camera *c) {
    return tc_v3(sinf(c->yaw) * cosf(c->pitch), cosf(c->yaw) * cosf(c->pitch), sinf(c->pitch));
}

tc_vec3 camera_right(const camera *c) { return tc_v3(cosf(c->yaw), -sinf(c->yaw), 0); }

static tc_mat4 look_at(tc_vec3 eye, tc_vec3 center, tc_vec3 up) {
    tc_vec3 f = tc_v3_normalize(tc_v3_sub(center, eye));
    tc_vec3 s = tc_v3_normalize(tc_v3_cross(f, up));
    tc_vec3 u = tc_v3_cross(s, f);
    tc_mat4 r = {{s.x, u.x, -f.x, 0,
                  s.y, u.y, -f.y, 0,
                  s.z, u.z, -f.z, 0,
                  -tc_v3_dot(s, eye), -tc_v3_dot(u, eye), tc_v3_dot(f, eye), 1}};
    return r;
}

static tc_mat4 perspective(float fov_y, float aspect, float near_z, float far_z) {
    float y = 1 / tanf(fov_y / 2), x = y / aspect, z = far_z / (near_z - far_z);
    tc_mat4 r = {{x, 0, 0, 0,
                  0, y, 0, 0,
                  0, 0, z, -1,
                  0, 0, z * near_z, 0}};
    return r;
}

tc_mat4 camera_view_projection(const camera *c, float aspect) {
    tc_mat4 p = perspective(c->vertical_fov, aspect, c->near_z, c->far_z);
    tc_mat4 v = look_at(c->position, tc_v3_add(c->position, camera_forward(c)), tc_v3(0, 0, 1));
    return tc_mat4_mul(&p, &v);
}
