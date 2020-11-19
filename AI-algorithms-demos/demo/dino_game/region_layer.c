#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <math.h>
#include <stdio.h>

#define H (15)
#define W (20)

inline static float *get_x(const float *const p)
{
    return p + H * W;
}
inline static float *get_y(const float *const p)
{
    return p + H * W * 2;
}
inline static float *get_w(const float *const p)
{
    return p + H * W * 3;
}
inline static float *get_h(const float *const p)
{
    return p + H * W * 4;
}

static float overlap(float *x1, float *w1, float *x2, float *w2)
{
    float l1 = *x1 - *w1 / 2;
    float l2 = *x2 - *w2 / 2;
    float left = l1 > l2 ? l1 : l2;
    float r1 = *x1 + *w1 / 2;
    float r2 = *x2 + *w2 / 2;
    float right = r1 < r2 ? r1 : r2;

    return right - left;
}

static float box_intersection(float *a, float *b)
{
    float w = overlap(get_x(a), get_w(a), get_x(b), get_w(b));
    float h = overlap(get_y(a), get_h(a), get_y(b), get_h(b));

    if (w < 0 || h < 0)
        return 0;
    return w * h;
}

static float box_union(const float *a, const float *b)
{
    float i = box_intersection(a, b);
    float u = *get_w(a) * *get_h(a) + *get_w(b) * *get_h(b) - i;

    return u;
}

static float box_iou(const float *a, const float *b)
{
    float i = box_intersection(a, b);
    float u = *get_w(a) * *get_h(a) + *get_w(b) * *get_h(b) - i;

    return i / u;
}

static float sigmoid(const float x)
{
    return 1. / (1. + expf(-x));
}

static uint16_t get_candidates(const float *candidates[],
                               const float logits[],
                               const uint16_t h,
                               const uint16_t w,
                               const uint16_t n_anchor,
                               const float score_thresh)
{
    uint16_t n_candidate = 0;
    for (uint16_t n = 0; n < n_anchor; ++n)
    {
        const float *p = logits + n * h * w * 5;
        for (uint16_t loc_h = 0; loc_h < h; ++loc_h)
        {
            for (uint16_t loc_w = 0; loc_w < w; ++loc_w, ++p)
            {
                if (*p < 5) // should take sigmoid and < score_thresh
                    continue;
                candidates[n_candidate++] = p;
            }
        }
    }
    return n_candidate;
}

static int candidate_cmp_fn(const void *a, const void *b)
{
    return (**(float **)b - **(float **)a);
}
static void sort_candidates(const float *candidates[], const uint16_t n_candidate)
{
    qsort(candidates, n_candidate, sizeof(float *), candidate_cmp_fn);
}

static void disable_non_max(float *const candidates[], const uint16_t n_candidate, float thresh)
{
    for (uint16_t c = 0; c < n_candidate; ++c)
    {
        if (*candidates[c] == 0)
            continue;
        for (uint16_t r = c + 1; r < n_candidate; ++r)
        {
            if (*candidates[r] == 0)
                continue;
            if (box_iou(candidates[c], candidates[r]) > thresh)
                *candidates[r] = 0;
        }
    }
}

static uint16_t copy_boxes(float *boxes[], uint16_t n_box_limit, float *const candidates[], uint16_t n_candidate)
{
    uint16_t box_count = 0;
    for (uint16_t c = 0; c < n_candidate; ++c)
    {
        if (box_count == n_box_limit)
            return n_box_limit;
        if (*candidates[c] != 0)
        {
            boxes[box_count++] = candidates[c];
        }
    }
    return box_count;
}
static uint16_t non_max_suppression(float boxes[],
                                    const uint16_t n_box_limit,
                                    float *const candidates[],
                                    const uint16_t n_candidate,
                                    const uint16_t h,
                                    const uint16_t w,
                                    const float thresh)
{
    sort_candidates(candidates, n_candidate);
    disable_non_max(candidates, n_candidate, thresh);
    uint16_t n_result = copy_boxes(boxes, n_box_limit, candidates, n_candidate);

    return n_result;
}

static void decode_ccwh(float *logits,
                        float *candidates[],
                        const uint16_t n_candidate,
                        const float anchors[][4],
                        const uint16_t n_anchor,
                        const uint16_t h,
                        const uint16_t w)
{
    for (uint16_t c = 0; c < n_candidate; ++c)
    {
        float *px = get_x(candidates[c]);
        float *py = get_y(candidates[c]);
        float *pw = get_w(candidates[c]);
        float *ph = get_h(candidates[c]);
        uint16_t anchor_index = (candidates[c] - logits) / (h * w * 5);
        uint16_t shift_x = (candidates[c] - logits) % w;
        uint16_t shift_y = (candidates[c] - logits) % (h * w) / w;
        shift_x *= 16;
        shift_y *= 16;
        *px = (*px - 3) * anchors[anchor_index][2] + anchors[anchor_index][0] + shift_x;
        *py = (*py - 3) * anchors[anchor_index][3] + anchors[anchor_index][1] + shift_y;
        *pw = expf(*pw - 3) * anchors[anchor_index][2];
        *ph = expf(*ph - 3) * anchors[anchor_index][3];
    }
}

static uint16_t get_boxes(float *boxes[],
                          uint16_t n_box_limit,
                          float *logits,
                          uint16_t h,
                          uint16_t w,
                          float anchors[][4],
                          uint16_t n_anchor,
                          const float score_thresh,
                          const float nms_thresh)
{
    static float *candidates[H * W * 15] = {NULL};
    uint16_t n_candidate = get_candidates(candidates, logits, h, w, n_anchor, score_thresh);
    decode_ccwh(logits, candidates, n_candidate, anchors, n_anchor, h, w);
    uint16_t n_result = non_max_suppression(boxes, n_box_limit, candidates, n_candidate, h, w, nms_thresh);
    return n_result;
}
