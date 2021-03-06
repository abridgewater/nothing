#include <stdlib.h>
#include <stdbool.h>

#include "game/camera.h"
#include "game/level/platforms.h"
#include "system/lt.h"
#include "system/nth_alloc.h"
#include "system/stacktrace.h"

#include "./rigid_bodies.h"

struct RigidBodies
{
    Lt *lt;
    size_t capacity;
    size_t count;

    Vec *positions;
    Vec *velocities;
    Vec *movements;
    Vec *sizes;
    Color *colors;
    bool *grounded;
    Vec *forces;
};

static const Vec opposing_rect_side_forces[RECT_SIDE_N] = {
    { .x = 1.0f,  .y =  0.0f  },  /* RECT_SIDE_LEFT = 0, */
    { .x = -1.0f, .y =  0.0f  },  /* RECT_SIDE_RIGHT, */
    { .x = 0.0f,  .y =  1.0f, },  /* RECT_SIDE_TOP, */
    { .x = 0.0f,  .y = -1.0f, }   /* RECT_SIDE_BOTTOM, */
};

static Vec opposing_force_by_sides(int sides[RECT_SIDE_N])
{
    Vec opposing_force = {
        .x = 0.0f,
        .y = 0.0f
    };

    for (Rect_side side = 0; side < RECT_SIDE_N; ++side) {
        if (sides[side]) {
            vec_add(
                &opposing_force,
                opposing_rect_side_forces[side]);
        }
    }

    return opposing_force;
}

RigidBodies *create_rigid_bodies(size_t capacity)
{
    Lt *lt = create_lt();
    if (lt == NULL) {
        return NULL;
    }

    RigidBodies *rigid_bodies = PUSH_LT(lt, nth_calloc(1, sizeof(RigidBodies)), free);
    if (rigid_bodies == NULL) {
        RETURN_LT(lt, NULL);
    }
    rigid_bodies->lt = lt;

    rigid_bodies->capacity = capacity;
    rigid_bodies->count = 0;

    rigid_bodies->positions = PUSH_LT(lt, nth_calloc(capacity, sizeof(Vec)), free);
    if (rigid_bodies->positions == NULL) {
        RETURN_LT(lt, NULL);
    }

    rigid_bodies->velocities = PUSH_LT(lt, nth_calloc(capacity, sizeof(Vec)), free);
    if (rigid_bodies->velocities == NULL) {
        RETURN_LT(lt, NULL);
    }

    rigid_bodies->movements = PUSH_LT(lt, nth_calloc(capacity, sizeof(Vec)), free);
    if (rigid_bodies->movements == NULL) {
        RETURN_LT(lt, NULL);
    }

    rigid_bodies->sizes = PUSH_LT(lt, nth_calloc(capacity, sizeof(Vec)), free);
    if (rigid_bodies->sizes == NULL) {
        RETURN_LT(lt, NULL);
    }

    rigid_bodies->colors = PUSH_LT(lt, nth_calloc(capacity, sizeof(Color)), free);
    if (rigid_bodies->colors == NULL) {
        RETURN_LT(lt, NULL);
    }

    rigid_bodies->grounded = PUSH_LT(lt, nth_calloc(capacity, sizeof(bool)), free);
    if (rigid_bodies->grounded == NULL) {
        RETURN_LT(lt, NULL);
    }

    rigid_bodies->forces = PUSH_LT(lt, nth_calloc(capacity, sizeof(Vec)), free);
    if (rigid_bodies->forces == NULL) {
        RETURN_LT(lt, NULL);
    }

    return rigid_bodies;
}

void destroy_rigid_bodies(RigidBodies *rigid_bodies)
{
    trace_assert(rigid_bodies);
    RETURN_LT0(rigid_bodies->lt);
}

int rigid_bodies_collide_with_itself(RigidBodies *rigid_bodies)
{
    trace_assert(rigid_bodies);

    /* TODO(#647): rigid_bodies_collide_with_itself is not implemented */

    return 0;
}

int rigid_bodies_collide_with_platforms(
    RigidBodies *rigid_bodies,
    const Platforms *platforms)
{
    trace_assert(rigid_bodies);
    trace_assert(platforms);

    int sides[RECT_SIDE_N] = { 0, 0, 0, 0 };

    for (size_t i = 0; i < rigid_bodies->count; ++i) {
        memset(sides, 0, sizeof(int) * RECT_SIDE_N);

        platforms_touches_rect_sides(platforms, rigid_bodies_hitbox(rigid_bodies, i), sides);

        if (sides[RECT_SIDE_BOTTOM]) {
            rigid_bodies->grounded[i] = true;
        }

        Vec opforce_direction = opposing_force_by_sides(sides);

        /* It's an opposing force that we apply to platforms. But
         * since platforms are impenetrable, it's not needed
         * here. Plus we are trying to apply that force to the rigid
         * body itself, because I'm dumb. */
        /*
        rigid_bodies_apply_force(
            rigid_bodies, i,
            vec_scala_mult(
                vec_neg(vec_norm(opforce_direction)),
                vec_length(
                    vec_sum(rigid_bodies->velocities[i],
                            rigid_bodies->movements[i])) * 8.0f));
        */

        if (fabs(opforce_direction.x) > 1e-6 && (opforce_direction.x < 0.0f) != ((rigid_bodies->velocities[i].x + rigid_bodies->movements[i].x) < 0.0f)) {
            rigid_bodies->velocities[i].x = 0.0f;
            rigid_bodies->movements[i].x = 0.0f;
        }

        if (fabs(opforce_direction.y) > 1e-6 && (opforce_direction.y < 0.0f) != ((rigid_bodies->velocities[i].y + rigid_bodies->movements[i].y) < 0.0f)) {
            rigid_bodies->velocities[i].y = 0.0f;
            rigid_bodies->movements[i].y = 0.0f;

            if (vec_length(rigid_bodies->velocities[i]) > 1e-6) {
                rigid_bodies_apply_force(
                    rigid_bodies, i,
                    vec_scala_mult(
                        vec_neg(rigid_bodies->velocities[i]),
                        16.0f));
            }
        }

        for (int j = 0; j < 1000 && vec_length(opforce_direction) > 1e-6; ++j) {
            rigid_bodies->positions[i] = vec_sum(
                rigid_bodies->positions[i],
                vec_scala_mult(
                    opforce_direction,
                    1e-2f));

            memset(sides, 0, sizeof(int) * RECT_SIDE_N);
            platforms_touches_rect_sides(platforms, rigid_bodies_hitbox(rigid_bodies, i), sides);
            opforce_direction = opposing_force_by_sides(sides);
        }
    }

    return 0;
}

int rigid_bodies_update(RigidBodies *rigid_bodies,
                        float delta_time)
{
    trace_assert(rigid_bodies);

    memset(rigid_bodies->grounded, 0,
           sizeof(bool) * rigid_bodies->count);

    for (size_t i = 0; i < rigid_bodies->count; ++i) {
        rigid_bodies->velocities[i] = vec_sum(
            rigid_bodies->velocities[i],
            vec_scala_mult(
                rigid_bodies->forces[i],
                delta_time));
    }

    for (size_t i = 0; i < rigid_bodies->count; ++i) {
        rigid_bodies->positions[i] = vec_sum(
            rigid_bodies->positions[i],
            vec_scala_mult(
                vec_sum(
                    rigid_bodies->velocities[i],
                    rigid_bodies->movements[i]),
                delta_time));
    }

    memset(rigid_bodies->forces, 0,
           sizeof(Vec) * rigid_bodies->count);

    return 0;
}

int rigid_bodies_render(RigidBodies *rigid_bodies,
                        Camera *camera)
{
    trace_assert(rigid_bodies);
    trace_assert(camera);

    for (size_t i = 0; i < rigid_bodies->count; ++i) {
        if (camera_fill_rect(
                camera,
                rect_from_vecs(
                    rigid_bodies->positions[i],
                    rigid_bodies->sizes[i]),
                rigid_bodies->colors[i]) < 0) {
            return -1;
        }
    }

    return 0;
}

RigidBodyId rigid_bodies_add(RigidBodies *rigid_bodies,
                             Rect rect,
                             Color color)
{
    trace_assert(rigid_bodies);
    trace_assert(rigid_bodies->count < rigid_bodies->capacity);

    RigidBodyId id = rigid_bodies->count++;
    rigid_bodies->positions[id] = vec(rect.x, rect.y);
    rigid_bodies->sizes[id] = vec(rect.w, rect.h);
    rigid_bodies->colors[id] = color;

    return id;
}

Rect rigid_bodies_hitbox(const RigidBodies *rigid_bodies,
                         RigidBodyId id)
{
    trace_assert(rigid_bodies);
    trace_assert(id < rigid_bodies->count);

    return rect_from_vecs(rigid_bodies->positions[id],
                          rigid_bodies->sizes[id]);
}

void rigid_bodies_move(RigidBodies *rigid_bodies,
                       RigidBodyId id,
                       Vec movement)
{
    trace_assert(rigid_bodies);
    trace_assert(id < rigid_bodies->count);

    rigid_bodies->movements[id] = movement;
}

int rigid_bodies_touches_ground(const RigidBodies *rigid_bodies,
                                RigidBodyId id)
{
    trace_assert(rigid_bodies);
    trace_assert(id < rigid_bodies->count);

    return rigid_bodies->grounded[id];
}

void rigid_bodies_apply_omniforce(RigidBodies *rigid_bodies,
                                  Vec force)
{
    for (size_t i = 0; i < rigid_bodies->count; ++i) {
        rigid_bodies_apply_force(rigid_bodies, i, force);
    }
}

void rigid_bodies_apply_force(RigidBodies * rigid_bodies,
                              RigidBodyId id,
                              Vec force)
{
    trace_assert(rigid_bodies);
    trace_assert(id < rigid_bodies->count);

    rigid_bodies->forces[id] = vec_sum(rigid_bodies->forces[id], force);
}

void rigid_bodies_transform_velocity(RigidBodies *rigid_bodies,
                                     RigidBodyId id,
                                     mat3x3 trans_mat)
{
    trace_assert(rigid_bodies);
    trace_assert(id < rigid_bodies->count);

    rigid_bodies->velocities[id] = point_mat3x3_product(
        rigid_bodies->velocities[id],
        trans_mat);
}

void rigid_bodies_teleport_to(RigidBodies *rigid_bodies,
                              RigidBodyId id,
                              Vec position)
{
    trace_assert(rigid_bodies);
    trace_assert(id < rigid_bodies->count);

    rigid_bodies->positions[id] = position;
}

void rigid_bodies_damper(RigidBodies *rigid_bodies,
                         RigidBodyId id,
                         Vec v)
{
    trace_assert(rigid_bodies);
    trace_assert(id < rigid_bodies->count);

    rigid_bodies_apply_force(
        rigid_bodies, id,
        vec(
            rigid_bodies->velocities[id].x * v.x,
            rigid_bodies->velocities[id].y * v.y));
}
