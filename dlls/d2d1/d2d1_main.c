/*
 * Direct2D (d2d1.dll) Clean-Room Implementation
 * (True Cubic Beziers, Elliptical Arcs, Layer Stack, Supersampled Anti-Aliasing, Gradient Brushes)
 *
 * Clean-Room Spec: specs/d2d1/
 * Copyright (C) 2026 Spumante Project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "d2d1.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

WINE_DEFAULT_DEBUG_CHANNEL(d2d1);

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int d2d1_simulate_stub(void)
{
    const char *env = getenv("SPUMANTE_SIMULATE_STUB");
    return (env && strcmp(env, "1") == 0);
}

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float srgb_to_linear_channel(float c)
{
    c = clampf(c, 0.0f, 1.0f);
    if (c <= 0.04045f) {
        return c / 12.92f;
    }
    return powf((c + 0.055f) / 1.055f, 2.4f);
}

static float linear_to_srgb_channel(float c)
{
    c = clampf(c, 0.0f, 1.0f);
    if (c <= 0.0031308f) {
        return c * 12.92f;
    }
    return 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

static D2D1_POINT_2F transform_point(D2D1_POINT_2F pt, const D2D1_MATRIX_3X2_F *m)
{
    D2D1_POINT_2F res;
    if (!m) {
        return pt;
    }
    res.x = pt.x * m->_11 + pt.y * m->_21 + m->_31;
    res.y = pt.x * m->_12 + pt.y * m->_22 + m->_32;
    return res;
}

/*
 * Standard 12 d2d1.spec Exported Functions
 */
struct D2D1_FACTORY {
    D2D1_FACTORY_TYPE factory_type;
    D2D1_DEBUG_LEVEL debug_level;
};

HRESULT WINAPI D2D1CreateFactory(
    D2D1_FACTORY_TYPE factory_type,
    REFIID riid,
    const D2D1_FACTORY_OPTIONS *options,
    void **factory)
{
    D2D1_FACTORY *f;
    (void)riid;

    TRACE("(%d, %p, %p, %p)\n", (int)factory_type, (const void *)riid, (const void *)options, (void *)factory);

    if (d2d1_simulate_stub()) {
        return E_NOTIMPL;
    }
    if (!factory) {
        return E_INVALIDARG;
    }
    *factory = NULL;

    if ((uint32_t)factory_type > (uint32_t)D2D1_FACTORY_TYPE_MULTI_THREADED) {
        WARN("Invalid factory_type %d\n", (int)factory_type);
        return E_INVALIDARG;
    }
    if (options && (uint32_t)options->debugLevel > (uint32_t)D2D1_DEBUG_LEVEL_INFORMATION) {
        WARN("Invalid debugLevel %d\n", (int)options->debugLevel);
        return E_INVALIDARG;
    }

    f = (D2D1_FACTORY *)calloc(1, sizeof(*f));
    if (!f) {
        return E_OUTOFMEMORY;
    }
    f->factory_type = factory_type;
    f->debug_level = options ? options->debugLevel : D2D1_DEBUG_LEVEL_NONE;
    *factory = f;
    return S_OK;
}

void WINAPI d2d_factory_destroy(D2D1_FACTORY *factory)
{
    free(factory);
}

void WINAPI D2D1MakeRotateMatrix(
    float angle,
    D2D1_POINT_2F center,
    D2D1_MATRIX_3X2_F *matrix)
{
    float rad, s, c;
    if (!matrix) return;
    rad = angle * ((float)M_PI / 180.0f);
    s = sinf(rad);
    c = cosf(rad);
    matrix->_11 = c;
    matrix->_12 = s;
    matrix->_21 = -s;
    matrix->_22 = c;
    matrix->_31 = center.x - center.x * c + center.y * s;
    matrix->_32 = center.y - center.x * s - center.y * c;
}

void WINAPI D2D1MakeSkewMatrix(
    float angle_x,
    float angle_y,
    D2D1_POINT_2F center,
    D2D1_MATRIX_3X2_F *matrix)
{
    float tx, ty;
    if (!matrix) return;
    tx = tanf(angle_x * ((float)M_PI / 180.0f));
    ty = tanf(angle_y * ((float)M_PI / 180.0f));
    matrix->_11 = 1.0f;
    matrix->_12 = ty;
    matrix->_21 = tx;
    matrix->_22 = 1.0f;
    matrix->_31 = -center.y * tx;
    matrix->_32 = -center.x * ty;
}

BOOL WINAPI D2D1IsMatrixInvertible(const D2D1_MATRIX_3X2_F *matrix)
{
    float det;
    if (!matrix) return FALSE;
    det = matrix->_11 * matrix->_22 - matrix->_12 * matrix->_21;
    return fabsf(det) > 1e-6f ? TRUE : FALSE;
}

BOOL WINAPI D2D1InvertMatrix(D2D1_MATRIX_3X2_F *matrix)
{
    float det, inv_det;
    D2D1_MATRIX_3X2_F m;
    if (!matrix) return FALSE;
    det = matrix->_11 * matrix->_22 - matrix->_12 * matrix->_21;
    if (fabsf(det) <= 1e-6f) {
        return FALSE;
    }
    inv_det = 1.0f / det;
    m = *matrix;
    matrix->_11 =  m._22 * inv_det;
    matrix->_12 = -m._12 * inv_det;
    matrix->_21 = -m._21 * inv_det;
    matrix->_22 =  m._11 * inv_det;
    matrix->_31 = (m._21 * m._32 - m._22 * m._31) * inv_det;
    matrix->_32 = (m._12 * m._31 - m._11 * m._32) * inv_det;
    return TRUE;
}

D2D1_COLOR_F WINAPI D2D1ConvertColorSpace(
    D2D1_COLOR_SPACE src_color_space,
    D2D1_COLOR_SPACE dst_color_space,
    const D2D1_COLOR_F *color)
{
    D2D1_COLOR_F out = {0.0f, 0.0f, 0.0f, 1.0f};
    if (!color) return out;
    out = *color;
    if (src_color_space == dst_color_space) {
        return out;
    }
    if (src_color_space == D2D1_COLOR_SPACE_SRGB && dst_color_space == D2D1_COLOR_SPACE_SCRGB) {
        out.r = srgb_to_linear_channel(color->r);
        out.g = srgb_to_linear_channel(color->g);
        out.b = srgb_to_linear_channel(color->b);
    } else if (src_color_space == D2D1_COLOR_SPACE_SCRGB && dst_color_space == D2D1_COLOR_SPACE_SRGB) {
        out.r = linear_to_srgb_channel(color->r);
        out.g = linear_to_srgb_channel(color->g);
        out.b = linear_to_srgb_channel(color->b);
    }
    return out;
}

HRESULT WINAPI D2D1CreateDevice(
    void *dxgi_device,
    const void *creation_properties,
    void **d2d_device)
{
    (void)creation_properties;
    if (!dxgi_device || !d2d_device) {
        return E_INVALIDARG;
    }
    return D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, NULL, NULL, d2d_device);
}

HRESULT WINAPI D2D1CreateDeviceContext(
    void *dxgi_surface,
    const void *creation_properties,
    void **d2d_device_context)
{
    (void)creation_properties;
    if (!dxgi_surface || !d2d_device_context) {
        return E_INVALIDARG;
    }
    return d2d_render_target_create(64, 64, (D2D1_RENDER_TARGET **)d2d_device_context);
}

void WINAPI D2D1SinCos(float angle, float *s, float *c)
{
    if (s) *s = sinf(angle);
    if (c) *c = cosf(angle);
}

float WINAPI D2D1Tan(float angle)
{
    return tanf(angle);
}

float WINAPI D2D1Vec3Length(float x, float y, float z)
{
    return sqrtf(x * x + y * y + z * z);
}

float WINAPI D2D1ComputeMaximumScaleFactor(const D2D1_MATRIX_3X2_F *matrix)
{
    float a, b, c, d, p, q, r;
    if (!matrix) return 0.0f;
    a = matrix->_11;
    b = matrix->_12;
    c = matrix->_21;
    d = matrix->_22;
    p = a * a + b * b + c * c + d * d;
    q = a * a + b * b - c * c - d * d;
    r = 2.0f * (a * c + b * d);
    return sqrtf(0.5f * (p + sqrtf(q * q + r * r)));
}

/*
 * Direct2D Path Geometry: True Cubic Beziers & Elliptical Arcs
 */
typedef enum D2D_SEG_KIND {
    D2D_SEG_LINE = 0,
    D2D_SEG_CUBIC_BEZIER = 1
} D2D_SEG_KIND;

typedef struct D2D_SEGMENT {
    D2D_SEG_KIND kind;
    D2D1_POINT_2F p0;
    D2D1_POINT_2F p1;
    D2D1_POINT_2F p2;
    D2D1_POINT_2F p3;
} D2D_SEGMENT;

#define D2D_MAX_SEGMENTS 256
#define D2D_MAX_POLYLINE_POINTS 2048

struct D2D1_PATH_GEOMETRY {
    D2D_SEGMENT segments[D2D_MAX_SEGMENTS];
    uint32_t segment_count;
    uint32_t bezier_count;
    D2D1_POINT_2F start_point;
    D2D1_POINT_2F current_point;
    D2D1_FIGURE_BEGIN figure_begin;
    D2D1_FIGURE_END figure_end;
    BOOL in_figure;
    BOOL closed;
};

HRESULT WINAPI d2d_path_geometry_create(
    D2D1_FACTORY *factory,
    D2D1_PATH_GEOMETRY **out_geom)
{
    D2D1_PATH_GEOMETRY *g;
    if (d2d1_simulate_stub()) return E_NOTIMPL;
    if (!factory || !out_geom) return E_INVALIDARG;
    g = (D2D1_PATH_GEOMETRY *)calloc(1, sizeof(*g));
    if (!g) return E_OUTOFMEMORY;
    *out_geom = g;
    return S_OK;
}

void WINAPI d2d_path_geometry_destroy(D2D1_PATH_GEOMETRY *geom)
{
    free(geom);
}

HRESULT WINAPI d2d_path_geometry_begin_figure(
    D2D1_PATH_GEOMETRY *geom,
    D2D1_POINT_2F start_point,
    D2D1_FIGURE_BEGIN figure_begin)
{
    if (!geom) return E_INVALIDARG;
    if (geom->closed || geom->in_figure) return D2DERR_WRONG_STATE;
    geom->start_point = start_point;
    geom->current_point = start_point;
    geom->figure_begin = figure_begin;
    geom->in_figure = TRUE;
    return S_OK;
}

HRESULT WINAPI d2d_path_geometry_add_line(
    D2D1_PATH_GEOMETRY *geom,
    D2D1_POINT_2F point)
{
    D2D_SEGMENT *seg;
    if (!geom) return E_INVALIDARG;
    if (!geom->in_figure || geom->closed) return D2DERR_WRONG_STATE;
    if (geom->segment_count >= D2D_MAX_SEGMENTS) return E_OUTOFMEMORY;
    seg = &geom->segments[geom->segment_count++];
    seg->kind = D2D_SEG_LINE;
    seg->p0 = geom->current_point;
    seg->p1 = point;
    seg->p2 = point;
    seg->p3 = point;
    geom->current_point = point;
    return S_OK;
}

HRESULT WINAPI d2d_path_geometry_add_bezier(
    D2D1_PATH_GEOMETRY *geom,
    const D2D1_BEZIER_SEGMENT *bezier)
{
    D2D_SEGMENT *seg;
    if (d2d1_simulate_stub()) return E_NOTIMPL;
    if (!geom || !bezier) return E_INVALIDARG;
    if (!geom->in_figure || geom->closed) return D2DERR_WRONG_STATE;
    if (geom->segment_count >= D2D_MAX_SEGMENTS) return E_OUTOFMEMORY;

    seg = &geom->segments[geom->segment_count++];
    seg->kind = D2D_SEG_CUBIC_BEZIER;
    seg->p0 = geom->current_point;
    seg->p1 = bezier->point1;
    seg->p2 = bezier->point2;
    seg->p3 = bezier->point3;
    geom->current_point = bezier->point3;
    geom->bezier_count++;
    return S_OK;
}

HRESULT WINAPI d2d_path_geometry_add_quadratic_bezier(
    D2D1_PATH_GEOMETRY *geom,
    const D2D1_QUADRATIC_BEZIER_SEGMENT *bezier)
{
    D2D1_BEZIER_SEGMENT cubic;
    D2D1_POINT_2F p0;
    if (!geom || !bezier) return E_INVALIDARG;
    p0 = geom->current_point;
    /* Degree elevation from quadratic (P0, Q1, Q2) to exact cubic (P0, P1, P2, P3) */
    cubic.point1.x = p0.x + (2.0f / 3.0f) * (bezier->point1.x - p0.x);
    cubic.point1.y = p0.y + (2.0f / 3.0f) * (bezier->point1.y - p0.y);
    cubic.point2.x = bezier->point2.x + (2.0f / 3.0f) * (bezier->point1.x - bezier->point2.x);
    cubic.point2.y = bezier->point2.y + (2.0f / 3.0f) * (bezier->point1.y - bezier->point2.y);
    cubic.point3 = bezier->point2;
    return d2d_path_geometry_add_bezier(geom, &cubic);
}

/*
 * Convert endpoint-parameterized rotated elliptical arc (D2D1_ARC_SEGMENT)
 * into <= 90-degree exact cubic Bezier segments.
 */
HRESULT WINAPI d2d_path_geometry_add_arc(
    D2D1_PATH_GEOMETRY *geom,
    const D2D1_ARC_SEGMENT *arc)
{
    D2D1_POINT_2F p0, p1;
    float rx, ry, phi, cos_phi, sin_phi;
    float dx2, dy2, x1p, y1p;
    float rx2, ry2, x1p2, y1p2, lambda;
    float num, den, sq, sign_factor, cxp, cyp, cx, cy;
    float ux, uy, vx, vy, theta1, dtheta;
    int num_segs, i;

    if (d2d1_simulate_stub()) return E_NOTIMPL;
    if (!geom || !arc) return E_INVALIDARG;
    if (!geom->in_figure || geom->closed) return D2DERR_WRONG_STATE;

    p0 = geom->current_point;
    p1 = arc->point;
    if (fabsf(p0.x - p1.x) < 1e-6f && fabsf(p0.y - p1.y) < 1e-6f) {
        return S_OK;
    }

    rx = fabsf(arc->size.width);
    ry = fabsf(arc->size.height);
    if (rx < 1e-6f || ry < 1e-6f) {
        return d2d_path_geometry_add_line(geom, p1);
    }

    phi = arc->rotationAngle * ((float)M_PI / 180.0f);
    cos_phi = cosf(phi);
    sin_phi = sinf(phi);

    dx2 = (p0.x - p1.x) * 0.5f;
    dy2 = (p0.y - p1.y) * 0.5f;
    x1p =  cos_phi * dx2 + sin_phi * dy2;
    y1p = -sin_phi * dx2 + cos_phi * dy2;

    x1p2 = x1p * x1p;
    y1p2 = y1p * y1p;
    rx2 = rx * rx;
    ry2 = ry * ry;

    /* Scale radii up if endpoints are further apart than 2*r */
    lambda = x1p2 / rx2 + y1p2 / ry2;
    if (lambda > 1.0f) {
        float rt_lambda = sqrtf(lambda);
        rx *= rt_lambda;
        ry *= rt_lambda;
        rx2 = rx * rx;
        ry2 = ry * ry;
    }

    num = rx2 * ry2 - rx2 * y1p2 - ry2 * x1p2;
    den = rx2 * y1p2 + ry2 * x1p2;
    sq = (den > 1e-12f && num > 0.0f) ? sqrtf(num / den) : 0.0f;
    sign_factor = ((arc->arcSize == D2D1_ARC_SIZE_LARGE) ==
                   (arc->sweepDirection == D2D1_SWEEP_DIRECTION_CLOCKWISE)) ? -1.0f : 1.0f;

    cxp =  sign_factor * sq * (rx * y1p / ry);
    cyp = -sign_factor * sq * (ry * x1p / rx);

    cx = cos_phi * cxp - sin_phi * cyp + (p0.x + p1.x) * 0.5f;
    cy = sin_phi * cxp + cos_phi * cyp + (p0.y + p1.y) * 0.5f;

    ux = (x1p - cxp) / rx;
    uy = (y1p - cyp) / ry;
    vx = (-x1p - cxp) / rx;
    vy = (-y1p - cyp) / ry;

    theta1 = atan2f(uy, ux);
    dtheta = atan2f(ux * vy - uy * vx, ux * vx + uy * vy);

    if (arc->sweepDirection == D2D1_SWEEP_DIRECTION_CLOCKWISE && dtheta < 0.0f) {
        dtheta += 2.0f * (float)M_PI;
    } else if (arc->sweepDirection == D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE && dtheta > 0.0f) {
        dtheta -= 2.0f * (float)M_PI;
    }

    num_segs = (int)ceilf(fabsf(dtheta) / ((float)M_PI * 0.5f + 1e-4f));
    if (num_segs < 1) num_segs = 1;
    if (num_segs > 16) num_segs = 16;

    for (i = 0; i < num_segs; ++i) {
        float t0 = theta1 + dtheta * ((float)i / (float)num_segs);
        float t1 = theta1 + dtheta * ((float)(i + 1) / (float)num_segs);
        float dt = t1 - t0;
        float alpha = (4.0f / 3.0f) * tanf(dt * 0.25f);
        float c0 = cosf(t0), s0 = sinf(t0);
        float c1 = cosf(t1), s1 = sinf(t1);
        float q1x = c0 - alpha * s0;
        float q1y = s0 + alpha * c0;
        float q2x = c1 + alpha * s1;
        float q2y = s1 - alpha * c1;
        float q3x = c1;
        float q3y = s1;
        D2D1_BEZIER_SEGMENT bez;

        bez.point1.x = cx + cos_phi * (rx * q1x) - sin_phi * (ry * q1y);
        bez.point1.y = cy + sin_phi * (rx * q1x) + cos_phi * (ry * q1y);
        bez.point2.x = cx + cos_phi * (rx * q2x) - sin_phi * (ry * q2y);
        bez.point2.y = cy + sin_phi * (rx * q2x) + cos_phi * (ry * q2y);
        bez.point3.x = cx + cos_phi * (rx * q3x) - sin_phi * (ry * q3y);
        bez.point3.y = cy + sin_phi * (rx * q3x) + cos_phi * (ry * q3y);

        if (i == num_segs - 1) {
            bez.point3 = p1;
        }
        HRESULT hr = d2d_path_geometry_add_bezier(geom, &bez);
        if (hr != S_OK) return hr;
    }

    return S_OK;
}

HRESULT WINAPI d2d_path_geometry_end_figure(
    D2D1_PATH_GEOMETRY *geom,
    D2D1_FIGURE_END figure_end)
{
    if (!geom || !geom->in_figure) return D2DERR_WRONG_STATE;
    geom->figure_end = figure_end;
    if (figure_end == D2D1_FIGURE_END_CLOSED) {
        if (fabsf(geom->current_point.x - geom->start_point.x) > 1e-5f ||
            fabsf(geom->current_point.y - geom->start_point.y) > 1e-5f) {
            d2d_path_geometry_add_line(geom, geom->start_point);
        }
    }
    geom->in_figure = FALSE;
    return S_OK;
}

HRESULT WINAPI d2d_path_geometry_close(D2D1_PATH_GEOMETRY *geom)
{
    if (!geom || geom->in_figure || geom->closed) return D2DERR_WRONG_STATE;
    geom->closed = TRUE;
    return S_OK;
}

uint32_t WINAPI d2d_path_geometry_get_bezier_count(const D2D1_PATH_GEOMETRY *geom)
{
    return geom ? geom->bezier_count : 0;
}

static D2D1_POINT_2F eval_cubic_bezier(
    D2D1_POINT_2F p0,
    D2D1_POINT_2F p1,
    D2D1_POINT_2F p2,
    D2D1_POINT_2F p3,
    float t)
{
    float u = 1.0f - t;
    float b0 = u * u * u;
    float b1 = 3.0f * u * u * t;
    float b2 = 3.0f * u * t * t;
    float b3 = t * t * t;
    D2D1_POINT_2F pt;
    pt.x = b0 * p0.x + b1 * p1.x + b2 * p2.x + b3 * p3.x;
    pt.y = b0 * p0.y + b1 * p1.y + b2 * p2.y + b3 * p3.y;
    return pt;
}

static void expand_bounds_point(D2D1_RECT_F *b, D2D1_POINT_2F p, BOOL *first)
{
    if (*first) {
        b->left = b->right = p.x;
        b->top = b->bottom = p.y;
        *first = FALSE;
    } else {
        if (p.x < b->left) b->left = p.x;
        if (p.x > b->right) b->right = p.x;
        if (p.y < b->top) b->top = p.y;
        if (p.y > b->bottom) b->bottom = p.y;
    }
}

static void solve_cubic_extrema_1d(
    float p0,
    float p1,
    float p2,
    float p3,
    float roots[2],
    int *root_count)
{
    /* B'(t) / 3 = a*t^2 + b*t + c = 0 */
    float a = -p0 + 3.0f * p1 - 3.0f * p2 + p3;
    float b = 2.0f * (p0 - 2.0f * p1 + p2);
    float c = p1 - p0;
    *root_count = 0;

    if (fabsf(a) < 1e-6f) {
        if (fabsf(b) > 1e-6f) {
            float t = -c / b;
            if (t > 0.0f && t < 1.0f) {
                roots[(*root_count)++] = t;
            }
        }
        return;
    }

    float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) return;
    float rt = sqrtf(disc);
    float t1 = (-b - rt) / (2.0f * a);
    float t2 = (-b + rt) / (2.0f * a);
    if (t1 > 0.0f && t1 < 1.0f) roots[(*root_count)++] = t1;
    if (t2 > 0.0f && t2 < 1.0f && fabsf(t2 - t1) > 1e-6f) roots[(*root_count)++] = t2;
}

HRESULT WINAPI d2d_path_geometry_get_bounds(
    const D2D1_PATH_GEOMETRY *geom,
    const D2D1_MATRIX_3X2_F *transform,
    D2D1_RECT_F *bounds)
{
    BOOL first = TRUE;
    uint32_t i;

    if (d2d1_simulate_stub()) return E_NOTIMPL;
    if (!geom || !bounds) return E_INVALIDARG;
    if (geom->segment_count == 0) {
        bounds->left = bounds->top = 1.0f;
        bounds->right = bounds->bottom = 0.0f;
        return S_OK;
    }

    for (i = 0; i < geom->segment_count; ++i) {
        const D2D_SEGMENT *s = &geom->segments[i];
        D2D1_POINT_2F p0 = transform_point(s->p0, transform);
        D2D1_POINT_2F p3 = transform_point(s->p3, transform);
        expand_bounds_point(bounds, p0, &first);
        expand_bounds_point(bounds, p3, &first);

        if (s->kind == D2D_SEG_CUBIC_BEZIER) {
            D2D1_POINT_2F p1 = transform_point(s->p1, transform);
            D2D1_POINT_2F p2 = transform_point(s->p2, transform);
            float roots[2];
            int rcount = 0, r;

            solve_cubic_extrema_1d(p0.x, p1.x, p2.x, p3.x, roots, &rcount);
            for (r = 0; r < rcount; ++r) {
                expand_bounds_point(bounds, eval_cubic_bezier(p0, p1, p2, p3, roots[r]), &first);
            }
            solve_cubic_extrema_1d(p0.y, p1.y, p2.y, p3.y, roots, &rcount);
            for (r = 0; r < rcount; ++r) {
                expand_bounds_point(bounds, eval_cubic_bezier(p0, p1, p2, p3, roots[r]), &first);
            }
        }
    }
    return S_OK;
}

static void flatten_geometry_to_polyline(
    const D2D1_PATH_GEOMETRY *geom,
    const D2D1_MATRIX_3X2_F *transform,
    D2D1_POINT_2F *pts,
    uint32_t max_pts,
    uint32_t *out_count)
{
    uint32_t count = 0;
    uint32_t i;

    if (!geom || geom->segment_count == 0 || max_pts == 0) {
        *out_count = 0;
        return;
    }

    pts[count++] = transform_point(geom->segments[0].p0, transform);

    for (i = 0; i < geom->segment_count && count < max_pts; ++i) {
        const D2D_SEGMENT *s = &geom->segments[i];
        D2D1_POINT_2F p0 = transform_point(s->p0, transform);
        D2D1_POINT_2F p3 = transform_point(s->p3, transform);

        if (s->kind == D2D_SEG_LINE) {
            pts[count++] = p3;
        } else {
            D2D1_POINT_2F p1 = transform_point(s->p1, transform);
            D2D1_POINT_2F p2 = transform_point(s->p2, transform);
            const int steps = 32;
            int k;
            for (k = 1; k <= steps && count < max_pts; ++k) {
                float t = (float)k / (float)steps;
                pts[count++] = eval_cubic_bezier(p0, p1, p2, p3, t);
            }
        }
    }

    *out_count = count;
}

HRESULT WINAPI d2d_path_geometry_compute_length(
    const D2D1_PATH_GEOMETRY *geom,
    const D2D1_MATRIX_3X2_F *transform,
    float flattening_tolerance,
    float *length)
{
    D2D1_POINT_2F pts[D2D_MAX_POLYLINE_POINTS];
    uint32_t count = 0, i;
    float total = 0.0f;
    (void)flattening_tolerance;

    if (d2d1_simulate_stub()) return E_NOTIMPL;
    if (!geom || !length) return E_INVALIDARG;

    flatten_geometry_to_polyline(geom, transform, pts, D2D_MAX_POLYLINE_POINTS, &count);
    for (i = 1; i < count; ++i) {
        float dx = pts[i].x - pts[i - 1].x;
        float dy = pts[i].y - pts[i - 1].y;
        total += sqrtf(dx * dx + dy * dy);
    }
    *length = total;
    return S_OK;
}

HRESULT WINAPI d2d_path_geometry_compute_point_at_length(
    const D2D1_PATH_GEOMETRY *geom,
    float length,
    const D2D1_MATRIX_3X2_F *transform,
    float flattening_tolerance,
    D2D1_POINT_2F *point,
    D2D1_POINT_2F *unit_tangent)
{
    D2D1_POINT_2F pts[D2D_MAX_POLYLINE_POINTS];
    uint32_t count = 0, i;
    float accum = 0.0f;
    (void)flattening_tolerance;

    if (d2d1_simulate_stub()) return E_NOTIMPL;
    if (!geom) return E_INVALIDARG;

    flatten_geometry_to_polyline(geom, transform, pts, D2D_MAX_POLYLINE_POINTS, &count);
    if (count == 0) return E_INVALIDARG;

    for (i = 1; i < count; ++i) {
        float dx = pts[i].x - pts[i - 1].x;
        float dy = pts[i].y - pts[i - 1].y;
        float seg_len = sqrtf(dx * dx + dy * dy);
        if (seg_len < 1e-6f) continue;

        if (accum + seg_len >= length || i == count - 1) {
            float rem = clampf(length - accum, 0.0f, seg_len);
            float t = rem / seg_len;
            if (point) {
                point->x = pts[i - 1].x + t * dx;
                point->y = pts[i - 1].y + t * dy;
            }
            if (unit_tangent) {
                unit_tangent->x = dx / seg_len;
                unit_tangent->y = dy / seg_len;
            }
            return S_OK;
        }
        accum += seg_len;
    }

    if (point) *point = pts[count - 1];
    if (unit_tangent) {
        unit_tangent->x = 1.0f;
        unit_tangent->y = 0.0f;
    }
    return S_OK;
}

HRESULT WINAPI d2d_path_geometry_compute_area(
    const D2D1_PATH_GEOMETRY *geom,
    const D2D1_MATRIX_3X2_F *transform,
    float flattening_tolerance,
    float *area)
{
    D2D1_POINT_2F pts[D2D_MAX_POLYLINE_POINTS];
    uint32_t count = 0, i;
    float sum = 0.0f;
    (void)flattening_tolerance;

    if (d2d1_simulate_stub()) return E_NOTIMPL;
    if (!geom || !area) return E_INVALIDARG;

    flatten_geometry_to_polyline(geom, transform, pts, D2D_MAX_POLYLINE_POINTS, &count);
    if (count < 3) {
        *area = 0.0f;
        return S_OK;
    }
    for (i = 0; i < count; ++i) {
        D2D1_POINT_2F a = pts[i];
        D2D1_POINT_2F b = pts[(i + 1) % count];
        sum += (a.x * b.y - b.x * a.y);
    }
    *area = 0.5f * fabsf(sum);
    return S_OK;
}

static BOOL polyline_contains_point(const D2D1_POINT_2F *pts, uint32_t count, D2D1_POINT_2F pt)
{
    BOOL inside = FALSE;
    uint32_t i, j;
    if (count < 3) return FALSE;
    for (i = 0, j = count - 1; i < count; j = i++) {
        if (((pts[i].y > pt.y) != (pts[j].y > pt.y)) &&
            (pt.x < (pts[j].x - pts[i].x) * (pt.y - pts[i].y) / (pts[j].y - pts[i].y) + pts[i].x)) {
            inside = !inside;
        }
    }
    return inside;
}

static float point_to_segment_dist_sq(D2D1_POINT_2F p, D2D1_POINT_2F a, D2D1_POINT_2F b)
{
    float vx = b.x - a.x;
    float vy = b.y - a.y;
    float wx = p.x - a.x;
    float wy = p.y - a.y;
    float c1 = vx * wx + vy * wy;
    float c2 = vx * vx + vy * vy;
    float t = 0.0f;
    if (c2 > 1e-8f) {
        t = clampf(c1 / c2, 0.0f, 1.0f);
    }
    float proj_x = a.x + t * vx;
    float proj_y = a.y + t * vy;
    float dx = p.x - proj_x;
    float dy = p.y - proj_y;
    return dx * dx + dy * dy;
}

HRESULT WINAPI d2d_path_geometry_fill_contains_point(
    const D2D1_PATH_GEOMETRY *geom,
    D2D1_POINT_2F point,
    const D2D1_MATRIX_3X2_F *transform,
    float flattening_tolerance,
    BOOL *contains)
{
    D2D1_POINT_2F pts[D2D_MAX_POLYLINE_POINTS];
    uint32_t count = 0;
    (void)flattening_tolerance;

    if (d2d1_simulate_stub()) return E_NOTIMPL;
    if (!geom || !contains) return E_INVALIDARG;

    flatten_geometry_to_polyline(geom, transform, pts, D2D_MAX_POLYLINE_POINTS, &count);
    *contains = polyline_contains_point(pts, count, point);
    return S_OK;
}

HRESULT WINAPI d2d_path_geometry_stroke_contains_point(
    const D2D1_PATH_GEOMETRY *geom,
    D2D1_POINT_2F point,
    float stroke_width,
    const D2D1_MATRIX_3X2_F *transform,
    float flattening_tolerance,
    BOOL *contains)
{
    D2D1_POINT_2F pts[D2D_MAX_POLYLINE_POINTS];
    uint32_t count = 0, i;
    float half_w = stroke_width * 0.5f;
    float r2 = half_w * half_w;
    (void)flattening_tolerance;

    if (d2d1_simulate_stub()) return E_NOTIMPL;
    if (!geom || !contains || stroke_width <= 0.0f) return E_INVALIDARG;

    flatten_geometry_to_polyline(geom, transform, pts, D2D_MAX_POLYLINE_POINTS, &count);
    *contains = FALSE;
    for (i = 1; i < count; ++i) {
        if (point_to_segment_dist_sq(point, pts[i - 1], pts[i]) <= r2) {
            *contains = TRUE;
            return S_OK;
        }
    }
    return S_OK;
}

/*
 * Direct2D Gradient Stop Collection & Brushes
 */
#define D2D_MAX_GRADIENT_STOPS 32

struct D2D1_GRADIENT_STOP_COLLECTION {
    D2D1_GRADIENT_STOP stops[D2D_MAX_GRADIENT_STOPS];
    uint32_t stop_count;
    D2D1_GAMMA gamma;
    D2D1_EXTEND_MODE extend_mode;
};

typedef enum D2D_BRUSH_KIND {
    D2D_BRUSH_SOLID = 0,
    D2D_BRUSH_LINEAR_GRADIENT = 1,
    D2D_BRUSH_RADIAL_GRADIENT = 2
} D2D_BRUSH_KIND;

struct D2D1_BRUSH {
    D2D_BRUSH_KIND kind;
    float opacity;
    D2D1_COLOR_F solid_color;
    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES linear_props;
    D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES radial_props;
    D2D1_GRADIENT_STOP_COLLECTION *stops;
};

HRESULT WINAPI d2d_gradient_stop_collection_create(
    const D2D1_GRADIENT_STOP *stops,
    uint32_t stop_count,
    D2D1_GAMMA color_interpolation_gamma,
    D2D1_EXTEND_MODE extend_mode,
    D2D1_GRADIENT_STOP_COLLECTION **out_collection)
{
    D2D1_GRADIENT_STOP_COLLECTION *sc;
    uint32_t i;

    if (d2d1_simulate_stub()) return E_NOTIMPL;
    if (!stops || stop_count == 0 || stop_count > D2D_MAX_GRADIENT_STOPS || !out_collection) {
        return E_INVALIDARG;
    }

    sc = (D2D1_GRADIENT_STOP_COLLECTION *)calloc(1, sizeof(*sc));
    if (!sc) return E_OUTOFMEMORY;

    for (i = 0; i < stop_count; ++i) {
        sc->stops[i] = stops[i];
    }
    sc->stop_count = stop_count;
    sc->gamma = color_interpolation_gamma;
    sc->extend_mode = extend_mode;
    *out_collection = sc;
    return S_OK;
}

void WINAPI d2d_gradient_stop_collection_destroy(D2D1_GRADIENT_STOP_COLLECTION *collection)
{
    free(collection);
}

HRESULT WINAPI d2d_solid_color_brush_create(
    const D2D1_COLOR_F *color,
    float opacity,
    D2D1_BRUSH **out_brush)
{
    D2D1_BRUSH *b;
    if (d2d1_simulate_stub()) return E_NOTIMPL;
    if (!color || !out_brush) return E_INVALIDARG;
    b = (D2D1_BRUSH *)calloc(1, sizeof(*b));
    if (!b) return E_OUTOFMEMORY;
    b->kind = D2D_BRUSH_SOLID;
    b->opacity = clampf(opacity, 0.0f, 1.0f);
    b->solid_color = *color;
    *out_brush = b;
    return S_OK;
}

HRESULT WINAPI d2d_linear_gradient_brush_create(
    const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES *props,
    float opacity,
    D2D1_GRADIENT_STOP_COLLECTION *stops,
    D2D1_BRUSH **out_brush)
{
    D2D1_BRUSH *b;
    if (d2d1_simulate_stub()) return E_NOTIMPL;
    if (!props || !stops || !out_brush) return E_INVALIDARG;
    b = (D2D1_BRUSH *)calloc(1, sizeof(*b));
    if (!b) return E_OUTOFMEMORY;
    b->kind = D2D_BRUSH_LINEAR_GRADIENT;
    b->opacity = clampf(opacity, 0.0f, 1.0f);
    b->linear_props = *props;
    b->stops = stops;
    *out_brush = b;
    return S_OK;
}

HRESULT WINAPI d2d_radial_gradient_brush_create(
    const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES *props,
    float opacity,
    D2D1_GRADIENT_STOP_COLLECTION *stops,
    D2D1_BRUSH **out_brush)
{
    D2D1_BRUSH *b;
    if (d2d1_simulate_stub()) return E_NOTIMPL;
    if (!props || !stops || !out_brush) return E_INVALIDARG;
    b = (D2D1_BRUSH *)calloc(1, sizeof(*b));
    if (!b) return E_OUTOFMEMORY;
    b->kind = D2D_BRUSH_RADIAL_GRADIENT;
    b->opacity = clampf(opacity, 0.0f, 1.0f);
    b->radial_props = *props;
    b->stops = stops;
    *out_brush = b;
    return S_OK;
}

void WINAPI d2d_brush_destroy(D2D1_BRUSH *brush)
{
    free(brush);
}

static float apply_extend_mode(float t, D2D1_EXTEND_MODE mode)
{
    if (mode == D2D1_EXTEND_MODE_CLAMP) {
        return clampf(t, 0.0f, 1.0f);
    }
    if (mode == D2D1_EXTEND_MODE_WRAP) {
        float f = t - floorf(t);
        return clampf(f, 0.0f, 1.0f);
    }
    if (mode == D2D1_EXTEND_MODE_MIRROR) {
        float period = fmodf(fabsf(t), 2.0f);
        if (period > 1.0f) {
            period = 2.0f - period;
        }
        return clampf(period, 0.0f, 1.0f);
    }
    return clampf(t, 0.0f, 1.0f);
}

static D2D1_COLOR_F evaluate_stops(const D2D1_GRADIENT_STOP_COLLECTION *sc, float t)
{
    uint32_t i;
    D2D1_COLOR_F c0, c1, out;
    float p0, p1, u;

    t = apply_extend_mode(t, sc->extend_mode);
    if (sc->stop_count == 1 || t <= sc->stops[0].position) {
        return sc->stops[0].color;
    }
    if (t >= sc->stops[sc->stop_count - 1].position) {
        return sc->stops[sc->stop_count - 1].color;
    }

    c0 = sc->stops[0].color;
    c1 = sc->stops[sc->stop_count - 1].color;
    p0 = sc->stops[0].position;
    p1 = sc->stops[sc->stop_count - 1].position;

    for (i = 0; i + 1 < sc->stop_count; ++i) {
        if (t >= sc->stops[i].position && t <= sc->stops[i + 1].position) {
            c0 = sc->stops[i].color;
            c1 = sc->stops[i + 1].color;
            p0 = sc->stops[i].position;
            p1 = sc->stops[i + 1].position;
            break;
        }
    }

    u = (p1 - p0 > 1e-6f) ? ((t - p0) / (p1 - p0)) : 0.0f;
    u = clampf(u, 0.0f, 1.0f);

    if (sc->gamma == D2D1_GAMMA_1_0) {
        float r0 = srgb_to_linear_channel(c0.r);
        float g0 = srgb_to_linear_channel(c0.g);
        float b0 = srgb_to_linear_channel(c0.b);
        float r1 = srgb_to_linear_channel(c1.r);
        float g1 = srgb_to_linear_channel(c1.g);
        float b1 = srgb_to_linear_channel(c1.b);
        out.r = linear_to_srgb_channel(r0 + u * (r1 - r0));
        out.g = linear_to_srgb_channel(g0 + u * (g1 - g0));
        out.b = linear_to_srgb_channel(b0 + u * (b1 - b0));
        out.a = c0.a + u * (c1.a - c0.a);
    } else {
        out.r = c0.r + u * (c1.r - c0.r);
        out.g = c0.g + u * (c1.g - c0.g);
        out.b = c0.b + u * (c1.b - c0.b);
        out.a = c0.a + u * (c1.a - c0.a);
    }
    return out;
}

HRESULT WINAPI d2d_gradient_brush_sample(
    const D2D1_BRUSH *brush,
    D2D1_POINT_2F pt,
    D2D1_COLOR_F *out_color)
{
    D2D1_COLOR_F col;

    if (d2d1_simulate_stub()) return E_NOTIMPL;
    if (!brush || !out_color) return E_INVALIDARG;

    if (brush->kind == D2D_BRUSH_SOLID) {
        col = brush->solid_color;
        col.a *= brush->opacity;
        *out_color = col;
        return S_OK;
    }

    if (brush->kind == D2D_BRUSH_LINEAR_GRADIENT) {
        float vx = brush->linear_props.endPoint.x - brush->linear_props.startPoint.x;
        float vy = brush->linear_props.endPoint.y - brush->linear_props.startPoint.y;
        float len_sq = vx * vx + vy * vy;
        float t = 0.0f;
        if (len_sq > 1e-8f) {
            t = ((pt.x - brush->linear_props.startPoint.x) * vx +
                 (pt.y - brush->linear_props.startPoint.y) * vy) / len_sq;
        }
        col = evaluate_stops(brush->stops, t);
        col.a *= brush->opacity;
        *out_color = col;
        return S_OK;
    }

    if (brush->kind == D2D_BRUSH_RADIAL_GRADIENT) {
        float rx = fabsf(brush->radial_props.radiusX);
        float ry = fabsf(brush->radial_props.radiusY);
        if (rx < 1e-6f || ry < 1e-6f) {
            col = evaluate_stops(brush->stops, 1.0f);
            col.a *= brush->opacity;
            *out_color = col;
            return S_OK;
        }
        /* Normalize to unit circle around center: O is normalized focal origin, D is ray to sample */
        float ox = brush->radial_props.gradientOriginOffset.x / rx;
        float oy = brush->radial_props.gradientOriginOffset.y / ry;
        float px = (pt.x - brush->radial_props.center.x) / rx;
        float py = (pt.y - brush->radial_props.center.y) / ry;
        float dx = px - ox;
        float dy = py - oy;
        float dist_sq = dx * dx + dy * dy;
        float t = 0.0f;
        if (dist_sq > 1e-10f) {
            /* Ray O + s*D intersects unit circle |O + s*D|^2 = 1 for s > 0; gradient parameter t = 1/s */
            float a = dist_sq;
            float b = 2.0f * (ox * dx + oy * dy);
            float c = (ox * ox + oy * oy) - 1.0f;
            float disc = b * b - 4.0f * a * c;
            if (disc >= 0.0f) {
                float s_hit = (-b + sqrtf(disc)) / (2.0f * a);
                if (s_hit > 1e-6f) {
                    t = 1.0f / s_hit;
                }
            }
        }
        col = evaluate_stops(brush->stops, t);
        col.a *= brush->opacity;
        *out_color = col;
        return S_OK;
    }

    return E_INVALIDARG;
}

/*
 * Direct2D Layer Stack & Supersampled Anti-Aliased Render Target
 */
struct D2D1_LAYER {
    D2D1_SIZE_F size;
};

#define D2D_MAX_LAYER_STACK 8

typedef struct D2D_LAYER_ENTRY {
    D2D1_LAYER_PARAMETERS params;
    D2D1_COLOR_F *pixels;
} D2D_LAYER_ENTRY;

struct D2D1_RENDER_TARGET {
    uint32_t width;
    uint32_t height;
    D2D1_ANTIALIAS_MODE antialias_mode;
    D2D1_MATRIX_3X2_F transform;
    BOOL in_draw;
    D2D1_COLOR_F *root_pixels;
    D2D_LAYER_ENTRY layer_stack[D2D_MAX_LAYER_STACK];
    uint32_t layer_depth;
};

HRESULT WINAPI d2d_layer_create(D2D1_SIZE_F size, D2D1_LAYER **out_layer)
{
    D2D1_LAYER *l;
    if (d2d1_simulate_stub()) return E_NOTIMPL;
    if (!out_layer || size.width <= 0.0f || size.height <= 0.0f) return E_INVALIDARG;
    l = (D2D1_LAYER *)calloc(1, sizeof(*l));
    if (!l) return E_OUTOFMEMORY;
    l->size = size;
    *out_layer = l;
    return S_OK;
}

void WINAPI d2d_layer_destroy(D2D1_LAYER *layer)
{
    free(layer);
}

HRESULT WINAPI d2d_render_target_create(
    uint32_t width,
    uint32_t height,
    D2D1_RENDER_TARGET **out_rt)
{
    D2D1_RENDER_TARGET *rt;
    if (d2d1_simulate_stub()) return E_NOTIMPL;
    if (!out_rt || width == 0 || height == 0 || width > 4096 || height > 4096) {
        return E_INVALIDARG;
    }
    rt = (D2D1_RENDER_TARGET *)calloc(1, sizeof(*rt));
    if (!rt) return E_OUTOFMEMORY;
    rt->root_pixels = (D2D1_COLOR_F *)calloc((size_t)width * (size_t)height, sizeof(D2D1_COLOR_F));
    if (!rt->root_pixels) {
        free(rt);
        return E_OUTOFMEMORY;
    }
    rt->width = width;
    rt->height = height;
    rt->antialias_mode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
    rt->transform._11 = 1.0f;
    rt->transform._22 = 1.0f;
    *out_rt = rt;
    return S_OK;
}

void WINAPI d2d_render_target_destroy(D2D1_RENDER_TARGET *rt)
{
    uint32_t i;
    if (!rt) return;
    for (i = 0; i < rt->layer_depth; ++i) {
        free(rt->layer_stack[i].pixels);
    }
    free(rt->root_pixels);
    free(rt);
}

static D2D1_COLOR_F *active_surface(D2D1_RENDER_TARGET *rt)
{
    if (rt->layer_depth > 0) {
        return rt->layer_stack[rt->layer_depth - 1].pixels;
    }
    return rt->root_pixels;
}

static void blend_pixel(D2D1_COLOR_F *dst, D2D1_COLOR_F src, float coverage)
{
    float src_a = clampf(src.a * coverage, 0.0f, 1.0f);
    float inv_a = 1.0f - src_a;
    dst->r = src.r * src_a + dst->r * inv_a;
    dst->g = src.g * src_a + dst->g * inv_a;
    dst->b = src.b * src_a + dst->b * inv_a;
    dst->a = src_a + dst->a * inv_a;
}

void WINAPI d2d_render_target_begin_draw(D2D1_RENDER_TARGET *rt)
{
    uint32_t i;
    if (!rt) return;
    for (i = 0; i < rt->layer_depth; ++i) {
        free(rt->layer_stack[i].pixels);
        rt->layer_stack[i].pixels = NULL;
    }
    rt->layer_depth = 0;
    rt->in_draw = TRUE;
}

HRESULT WINAPI d2d_render_target_end_draw(D2D1_RENDER_TARGET *rt)
{
    uint32_t i;
    if (d2d1_simulate_stub()) return E_NOTIMPL;
    if (!rt || !rt->in_draw) return D2DERR_WRONG_STATE;
    rt->in_draw = FALSE;
    if (rt->layer_depth > 0) {
        for (i = 0; i < rt->layer_depth; ++i) {
            free(rt->layer_stack[i].pixels);
            rt->layer_stack[i].pixels = NULL;
        }
        rt->layer_depth = 0;
        return D2DERR_PUSH_POP_UNBALANCED;
    }
    return S_OK;
}

void WINAPI d2d_render_target_clear(
    D2D1_RENDER_TARGET *rt,
    const D2D1_COLOR_F *clear_color)
{
    D2D1_COLOR_F c = {0.0f, 0.0f, 0.0f, 0.0f};
    D2D1_COLOR_F *surf;
    uint32_t i, total;
    if (!rt) return;
    if (clear_color) c = *clear_color;
    surf = active_surface(rt);
    total = rt->width * rt->height;
    for (i = 0; i < total; ++i) {
        surf[i] = c;
    }
}

void WINAPI d2d_render_target_set_antialias_mode(
    D2D1_RENDER_TARGET *rt,
    D2D1_ANTIALIAS_MODE antialias_mode)
{
    if (!rt) return;
    rt->antialias_mode = antialias_mode;
}

D2D1_ANTIALIAS_MODE WINAPI d2d_render_target_get_antialias_mode(
    const D2D1_RENDER_TARGET *rt)
{
    return rt ? rt->antialias_mode : D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
}

void WINAPI d2d_render_target_set_transform(
    D2D1_RENDER_TARGET *rt,
    const D2D1_MATRIX_3X2_F *transform)
{
    if (!rt || !transform) return;
    rt->transform = *transform;
}

HRESULT WINAPI d2d_render_target_push_layer(
    D2D1_RENDER_TARGET *rt,
    const D2D1_LAYER_PARAMETERS *layer_parameters,
    D2D1_LAYER *layer)
{
    D2D_LAYER_ENTRY *entry;
    (void)layer;

    if (d2d1_simulate_stub()) return E_NOTIMPL;
    if (!rt || !layer_parameters) return E_INVALIDARG;
    if (!rt->in_draw) return D2DERR_WRONG_STATE;
    if (rt->layer_depth >= D2D_MAX_LAYER_STACK) return E_OUTOFMEMORY;

    entry = &rt->layer_stack[rt->layer_depth];
    entry->params = *layer_parameters;
    entry->pixels = (D2D1_COLOR_F *)calloc((size_t)rt->width * (size_t)rt->height, sizeof(D2D1_COLOR_F));
    if (!entry->pixels) return E_OUTOFMEMORY;

    rt->layer_depth++;
    return S_OK;
}

HRESULT WINAPI d2d_render_target_pop_layer(D2D1_RENDER_TARGET *rt)
{
    D2D_LAYER_ENTRY entry;
    D2D1_COLOR_F *dst_surf;
    D2D1_POINT_2F mask_pts[D2D_MAX_POLYLINE_POINTS];
    uint32_t mask_pt_count = 0;
    uint32_t x, y;

    if (d2d1_simulate_stub()) return E_NOTIMPL;
    if (!rt || !rt->in_draw) return D2DERR_WRONG_STATE;
    if (rt->layer_depth == 0) {
        return D2DERR_POP_CALL_DID_NOT_MATCH_PUSH;
    }

    rt->layer_depth--;
    entry = rt->layer_stack[rt->layer_depth];
    rt->layer_stack[rt->layer_depth].pixels = NULL;
    dst_surf = active_surface(rt);

    if (entry.params.geometricMask) {
        flatten_geometry_to_polyline(
            entry.params.geometricMask,
            &entry.params.maskTransform,
            mask_pts,
            D2D_MAX_POLYLINE_POINTS,
            &mask_pt_count);
    }

    for (y = 0; y < rt->height; ++y) {
        for (x = 0; x < rt->width; ++x) {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;
            float cov = clampf(entry.params.opacity, 0.0f, 1.0f);
            D2D1_COLOR_F src = entry.pixels[y * rt->width + x];

            if (px < entry.params.contentBounds.left ||
                px > entry.params.contentBounds.right ||
                py < entry.params.contentBounds.top ||
                py > entry.params.contentBounds.bottom) {
                continue;
            }

            if (entry.params.geometricMask) {
                D2D1_POINT_2F pt = {px, py};
                if (!polyline_contains_point(mask_pts, mask_pt_count, pt)) {
                    continue;
                }
            }

            if (entry.params.opacityBrush) {
                D2D1_POINT_2F pt = {px, py};
                D2D1_COLOR_F op_col = {1.0f, 1.0f, 1.0f, 1.0f};
                d2d_gradient_brush_sample(entry.params.opacityBrush, pt, &op_col);
                cov *= clampf(op_col.a, 0.0f, 1.0f);
            }

            blend_pixel(&dst_surf[y * rt->width + x], src, cov);
        }
    }

    free(entry.pixels);
    return S_OK;
}

HRESULT WINAPI d2d_render_target_fill_rectangle(
    D2D1_RENDER_TARGET *rt,
    const D2D1_RECT_F *rect,
    const D2D1_BRUSH *brush)
{
    D2D1_COLOR_F *surf;
    uint32_t x, y;

    if (d2d1_simulate_stub()) return E_NOTIMPL;
    if (!rt || !rect || !brush) return E_INVALIDARG;
    surf = active_surface(rt);

    for (y = 0; y < rt->height; ++y) {
        for (x = 0; x < rt->width; ++x) {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;
            if (px >= rect->left && px <= rect->right &&
                py >= rect->top && py <= rect->bottom) {
                D2D1_POINT_2F pt = {px, py};
                D2D1_COLOR_F col = {0};
                d2d_gradient_brush_sample(brush, pt, &col);
                blend_pixel(&surf[y * rt->width + x], col, 1.0f);
            }
        }
    }
    return S_OK;
}

HRESULT WINAPI d2d_render_target_fill_geometry(
    D2D1_RENDER_TARGET *rt,
    const D2D1_PATH_GEOMETRY *geometry,
    const D2D1_BRUSH *brush,
    const D2D1_BRUSH *opacity_brush)
{
    D2D1_POINT_2F pts[D2D_MAX_POLYLINE_POINTS];
    uint32_t count = 0;
    D2D1_COLOR_F *surf;
    uint32_t x, y;

    if (d2d1_simulate_stub()) return E_NOTIMPL;
    if (!rt || !geometry || !brush) return E_INVALIDARG;

    flatten_geometry_to_polyline(geometry, &rt->transform, pts, D2D_MAX_POLYLINE_POINTS, &count);
    if (count < 3) return S_OK;
    surf = active_surface(rt);

    for (y = 0; y < rt->height; ++y) {
        for (x = 0; x < rt->width; ++x) {
            float cov = 0.0f;
            if (rt->antialias_mode == D2D1_ANTIALIAS_MODE_PER_PRIMITIVE) {
                int sx, sy, hits = 0;
                for (sy = 0; sy < 4; ++sy) {
                    for (sx = 0; sx < 4; ++sx) {
                        D2D1_POINT_2F sp = {
                            (float)x + ((float)sx + 0.5f) * 0.25f,
                            (float)y + ((float)sy + 0.5f) * 0.25f
                        };
                        if (polyline_contains_point(pts, count, sp)) {
                            hits++;
                        }
                    }
                }
                cov = (float)hits / 16.0f;
            } else {
                D2D1_POINT_2F cp = {(float)x + 0.5f, (float)y + 0.5f};
                cov = polyline_contains_point(pts, count, cp) ? 1.0f : 0.0f;
            }

            if (cov > 0.0f) {
                D2D1_POINT_2F cp = {(float)x + 0.5f, (float)y + 0.5f};
                D2D1_COLOR_F col = {0};
                d2d_gradient_brush_sample(brush, cp, &col);
                if (opacity_brush) {
                    D2D1_COLOR_F op = {1.0f, 1.0f, 1.0f, 1.0f};
                    d2d_gradient_brush_sample(opacity_brush, cp, &op);
                    cov *= clampf(op.a, 0.0f, 1.0f);
                }
                blend_pixel(&surf[y * rt->width + x], col, cov);
            }
        }
    }
    return S_OK;
}

HRESULT WINAPI d2d_render_target_draw_geometry(
    D2D1_RENDER_TARGET *rt,
    const D2D1_PATH_GEOMETRY *geometry,
    const D2D1_BRUSH *brush,
    float stroke_width)
{
    D2D1_POINT_2F pts[D2D_MAX_POLYLINE_POINTS];
    uint32_t count = 0, i, x, y;
    float half_w, r2;
    D2D1_COLOR_F *surf;

    if (d2d1_simulate_stub()) return E_NOTIMPL;
    if (!rt || !geometry || !brush || stroke_width <= 0.0f) return E_INVALIDARG;

    flatten_geometry_to_polyline(geometry, &rt->transform, pts, D2D_MAX_POLYLINE_POINTS, &count);
    if (count < 2) return S_OK;
    half_w = stroke_width * 0.5f;
    r2 = half_w * half_w;
    surf = active_surface(rt);

    for (y = 0; y < rt->height; ++y) {
        for (x = 0; x < rt->width; ++x) {
            float cov = 0.0f;
            if (rt->antialias_mode == D2D1_ANTIALIAS_MODE_PER_PRIMITIVE) {
                int sx, sy, hits = 0;
                for (sy = 0; sy < 4; ++sy) {
                    for (sx = 0; sx < 4; ++sx) {
                        D2D1_POINT_2F sp = {
                            (float)x + ((float)sx + 0.5f) * 0.25f,
                            (float)y + ((float)sy + 0.5f) * 0.25f
                        };
                        for (i = 1; i < count; ++i) {
                            if (point_to_segment_dist_sq(sp, pts[i - 1], pts[i]) <= r2) {
                                hits++;
                                break;
                            }
                        }
                    }
                }
                cov = (float)hits / 16.0f;
            } else {
                D2D1_POINT_2F cp = {(float)x + 0.5f, (float)y + 0.5f};
                for (i = 1; i < count; ++i) {
                    if (point_to_segment_dist_sq(cp, pts[i - 1], pts[i]) <= r2) {
                        cov = 1.0f;
                        break;
                    }
                }
            }

            if (cov > 0.0f) {
                D2D1_POINT_2F cp = {(float)x + 0.5f, (float)y + 0.5f};
                D2D1_COLOR_F col = {0};
                d2d_gradient_brush_sample(brush, cp, &col);
                blend_pixel(&surf[y * rt->width + x], col, cov);
            }
        }
    }
    return S_OK;
}

HRESULT WINAPI d2d_render_target_draw_line(
    D2D1_RENDER_TARGET *rt,
    D2D1_POINT_2F p0,
    D2D1_POINT_2F p1,
    const D2D1_BRUSH *brush,
    float stroke_width)
{
    D2D1_FACTORY f = {D2D1_FACTORY_TYPE_SINGLE_THREADED, D2D1_DEBUG_LEVEL_NONE};
    D2D1_PATH_GEOMETRY *g = NULL;
    HRESULT hr = d2d_path_geometry_create(&f, &g);
    if (hr != S_OK) return hr;
    d2d_path_geometry_begin_figure(g, p0, D2D1_FIGURE_BEGIN_HOLLOW);
    d2d_path_geometry_add_line(g, p1);
    d2d_path_geometry_end_figure(g, D2D1_FIGURE_END_OPEN);
    d2d_path_geometry_close(g);
    hr = d2d_render_target_draw_geometry(rt, g, brush, stroke_width);
    d2d_path_geometry_destroy(g);
    return hr;
}

HRESULT WINAPI d2d_render_target_read_pixel(
    const D2D1_RENDER_TARGET *rt,
    uint32_t x,
    uint32_t y,
    D2D1_COLOR_F *out_color)
{
    const D2D1_COLOR_F *surf;
    if (d2d1_simulate_stub()) return E_NOTIMPL;
    if (!rt || !out_color || x >= rt->width || y >= rt->height) {
        return E_INVALIDARG;
    }
    surf = (rt->layer_depth > 0) ? rt->layer_stack[rt->layer_depth - 1].pixels : rt->root_pixels;
    *out_color = surf[y * rt->width + x];
    return S_OK;
}
