/*
 * Direct2D (d2d1.dll) & vkd3d-shader Metal Shading Language (MSL) Public Definitions
 *
 * Clean-Room Spec: specs/d2d1/
 * Copyright (C) 2026 Wine Parity Project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef _WINE_D2D1_PARITY_H
#define _WINE_D2D1_PARITY_H

#include "../../tests/include/parity_test.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef WINE_DEFAULT_DEBUG_CHANNEL
#define WINE_DEFAULT_DEBUG_CHANNEL(ch)
#endif
#ifndef TRACE
#define TRACE(...) do { } while (0)
#endif
#ifndef WARN
#define WARN(...)  do { } while (0)
#endif
#ifndef FIXME
#define FIXME(...) do { } while (0)
#endif
#ifndef ERR
#define ERR(...)   do { } while (0)
#endif

#ifndef _WIN32
typedef int32_t HRESULT;
#endif

#ifndef S_OK
#define S_OK ((HRESULT)0x00000000L)
#endif
#ifndef S_FALSE
#define S_FALSE ((HRESULT)0x00000001L)
#endif
#ifndef E_NOTIMPL
#define E_NOTIMPL ((HRESULT)0x80004001L)
#endif
#ifndef E_NOINTERFACE
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#endif
#ifndef E_POINTER
#define E_POINTER ((HRESULT)0x80004003L)
#endif
#ifndef E_FAIL
#define E_FAIL ((HRESULT)0x80004005L)
#endif
#ifndef E_OUTOFMEMORY
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#endif
#ifndef E_INVALIDARG
#define E_INVALIDARG ((HRESULT)0x80070057L)
#endif

/* Direct2D HRESULT error codes */
#ifndef D2DERR_WRONG_STATE
#define D2DERR_WRONG_STATE                  ((HRESULT)0x88990001L)
#endif
#ifndef D2DERR_NOT_INITIALIZED
#define D2DERR_NOT_INITIALIZED              ((HRESULT)0x88990002L)
#endif
#ifndef D2DERR_UNSUPPORTED_OPERATION
#define D2DERR_UNSUPPORTED_OPERATION        ((HRESULT)0x88990003L)
#endif
#ifndef D2DERR_SCANNER_FAILED
#define D2DERR_SCANNER_FAILED               ((HRESULT)0x88990004L)
#endif
#ifndef D2DERR_POP_CALL_DID_NOT_MATCH_PUSH
#define D2DERR_POP_CALL_DID_NOT_MATCH_PUSH  ((HRESULT)0x88990014L)
#endif
#ifndef D2DERR_PUSH_POP_UNBALANCED
#define D2DERR_PUSH_POP_UNBALANCED          ((HRESULT)0x88990016L)
#endif

#ifndef D2D1_DEFAULT_FLATTENING_TOLERANCE
#define D2D1_DEFAULT_FLATTENING_TOLERANCE   (0.25f)
#endif

#ifndef _REFGUID_DEFINED
#define _REFGUID_DEFINED
typedef struct _D2D_GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
} GUID;
typedef const GUID *REFGUID;
typedef const GUID *REFIID;
#endif

typedef enum D2D1_FACTORY_TYPE {
    D2D1_FACTORY_TYPE_SINGLE_THREADED = 0,
    D2D1_FACTORY_TYPE_MULTI_THREADED = 1,
    D2D1_FACTORY_TYPE_FORCE_DWORD = 0xffffffff
} D2D1_FACTORY_TYPE;

typedef enum D2D1_DEBUG_LEVEL {
    D2D1_DEBUG_LEVEL_NONE = 0,
    D2D1_DEBUG_LEVEL_ERROR = 1,
    D2D1_DEBUG_LEVEL_WARNING = 2,
    D2D1_DEBUG_LEVEL_INFORMATION = 3,
    D2D1_DEBUG_LEVEL_FORCE_DWORD = 0xffffffff
} D2D1_DEBUG_LEVEL;

typedef struct D2D1_FACTORY_OPTIONS {
    D2D1_DEBUG_LEVEL debugLevel;
} D2D1_FACTORY_OPTIONS;

typedef enum D2D1_COLOR_SPACE {
    D2D1_COLOR_SPACE_CUSTOM = 0,
    D2D1_COLOR_SPACE_SRGB = 1,
    D2D1_COLOR_SPACE_SCRGB = 2,
    D2D1_COLOR_SPACE_FORCE_DWORD = 0xffffffff
} D2D1_COLOR_SPACE;

typedef enum D2D1_GAMMA {
    D2D1_GAMMA_2_2 = 0,
    D2D1_GAMMA_1_0 = 1,
    D2D1_GAMMA_FORCE_DWORD = 0xffffffff
} D2D1_GAMMA;

typedef enum D2D1_EXTEND_MODE {
    D2D1_EXTEND_MODE_CLAMP = 0,
    D2D1_EXTEND_MODE_WRAP = 1,
    D2D1_EXTEND_MODE_MIRROR = 2,
    D2D1_EXTEND_MODE_FORCE_DWORD = 0xffffffff
} D2D1_EXTEND_MODE;

typedef enum D2D1_ANTIALIAS_MODE {
    D2D1_ANTIALIAS_MODE_PER_PRIMITIVE = 0,
    D2D1_ANTIALIAS_MODE_ALIASED = 1,
    D2D1_ANTIALIAS_MODE_FORCE_DWORD = 0xffffffff
} D2D1_ANTIALIAS_MODE;

typedef enum D2D1_SWEEP_DIRECTION {
    D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE = 0,
    D2D1_SWEEP_DIRECTION_CLOCKWISE = 1,
    D2D1_SWEEP_DIRECTION_FORCE_DWORD = 0xffffffff
} D2D1_SWEEP_DIRECTION;

typedef enum D2D1_ARC_SIZE {
    D2D1_ARC_SIZE_SMALL = 0,
    D2D1_ARC_SIZE_LARGE = 1,
    D2D1_ARC_SIZE_FORCE_DWORD = 0xffffffff
} D2D1_ARC_SIZE;

typedef enum D2D1_FIGURE_BEGIN {
    D2D1_FIGURE_BEGIN_FILLED = 0,
    D2D1_FIGURE_BEGIN_HOLLOW = 1,
    D2D1_FIGURE_BEGIN_FORCE_DWORD = 0xffffffff
} D2D1_FIGURE_BEGIN;

typedef enum D2D1_FIGURE_END {
    D2D1_FIGURE_END_OPEN = 0,
    D2D1_FIGURE_END_CLOSED = 1,
    D2D1_FIGURE_END_FORCE_DWORD = 0xffffffff
} D2D1_FIGURE_END;

typedef enum D2D1_LAYER_OPTIONS {
    D2D1_LAYER_OPTIONS_NONE = 0x00000000,
    D2D1_LAYER_OPTIONS_INITIALIZE_FOR_CLEARTYPE = 0x00000001,
    D2D1_LAYER_OPTIONS_FORCE_DWORD = 0xffffffff
} D2D1_LAYER_OPTIONS;

typedef struct D2D_POINT_2F {
    float x;
    float y;
} D2D_POINT_2F, D2D1_POINT_2F;

typedef struct D2D_SIZE_F {
    float width;
    float height;
} D2D_SIZE_F, D2D1_SIZE_F;

typedef struct D2D_SIZE_U {
    uint32_t width;
    uint32_t height;
} D2D_SIZE_U, D2D1_SIZE_U;

typedef struct D2D_RECT_F {
    float left;
    float top;
    float right;
    float bottom;
} D2D_RECT_F, D2D1_RECT_F;

typedef struct D2D_COLOR_F {
    float r;
    float g;
    float b;
    float a;
} D2D_COLOR_F, D2D1_COLOR_F;

typedef struct D2D_MATRIX_3X2_F {
    union {
        struct {
            float _11, _12;
            float _21, _22;
            float _31, _32;
        };
        struct {
            float m11, m12;
            float m21, m22;
            float dx, dy;
        };
        float m[3][2];
    };
} D2D_MATRIX_3X2_F, D2D1_MATRIX_3X2_F;

typedef struct D2D1_BEZIER_SEGMENT {
    D2D1_POINT_2F point1;
    D2D1_POINT_2F point2;
    D2D1_POINT_2F point3;
} D2D1_BEZIER_SEGMENT;

typedef struct D2D1_QUADRATIC_BEZIER_SEGMENT {
    D2D1_POINT_2F point1;
    D2D1_POINT_2F point2;
} D2D1_QUADRATIC_BEZIER_SEGMENT;

typedef struct D2D1_ARC_SEGMENT {
    D2D1_POINT_2F point;
    D2D1_SIZE_F size;
    float rotationAngle;
    D2D1_SWEEP_DIRECTION sweepDirection;
    D2D1_ARC_SIZE arcSize;
} D2D1_ARC_SEGMENT;

typedef struct D2D1_GRADIENT_STOP {
    float position;
    D2D1_COLOR_F color;
} D2D1_GRADIENT_STOP;

typedef struct D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES {
    D2D1_POINT_2F startPoint;
    D2D1_POINT_2F endPoint;
} D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES;

typedef struct D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES {
    D2D1_POINT_2F center;
    D2D1_POINT_2F gradientOriginOffset;
    float radiusX;
    float radiusY;
} D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES;

typedef struct D2D1_FACTORY D2D1_FACTORY;
typedef struct D2D1_PATH_GEOMETRY D2D1_PATH_GEOMETRY;
typedef struct D2D1_GRADIENT_STOP_COLLECTION D2D1_GRADIENT_STOP_COLLECTION;
typedef struct D2D1_BRUSH D2D1_BRUSH;
typedef struct D2D1_LAYER D2D1_LAYER;
typedef struct D2D1_RENDER_TARGET D2D1_RENDER_TARGET;

typedef struct D2D1_LAYER_PARAMETERS {
    D2D1_RECT_F contentBounds;
    D2D1_PATH_GEOMETRY *geometricMask;
    D2D1_ANTIALIAS_MODE maskAntialiasMode;
    D2D1_MATRIX_3X2_F maskTransform;
    float opacity;
    D2D1_BRUSH *opacityBrush;
    D2D1_LAYER_OPTIONS layerOptions;
} D2D1_LAYER_PARAMETERS;

/*
 * Standard 12 d2d1.spec DLL exports
 */
HRESULT WINAPI D2D1CreateFactory(
    D2D1_FACTORY_TYPE factory_type,
    REFIID riid,
    const D2D1_FACTORY_OPTIONS *options,
    void **factory);

void WINAPI D2D1MakeRotateMatrix(
    float angle,
    D2D1_POINT_2F center,
    D2D1_MATRIX_3X2_F *matrix);

void WINAPI D2D1MakeSkewMatrix(
    float angle_x,
    float angle_y,
    D2D1_POINT_2F center,
    D2D1_MATRIX_3X2_F *matrix);

BOOL WINAPI D2D1IsMatrixInvertible(const D2D1_MATRIX_3X2_F *matrix);
BOOL WINAPI D2D1InvertMatrix(D2D1_MATRIX_3X2_F *matrix);

D2D1_COLOR_F WINAPI D2D1ConvertColorSpace(
    D2D1_COLOR_SPACE src_color_space,
    D2D1_COLOR_SPACE dst_color_space,
    const D2D1_COLOR_F *color);

HRESULT WINAPI D2D1CreateDevice(
    void *dxgi_device,
    const void *creation_properties,
    void **d2d_device);

HRESULT WINAPI D2D1CreateDeviceContext(
    void *dxgi_surface,
    const void *creation_properties,
    void **d2d_device_context);

void WINAPI D2D1SinCos(float angle, float *s, float *c);
float WINAPI D2D1Tan(float angle);
float WINAPI D2D1Vec3Length(float x, float y, float z);
float WINAPI D2D1ComputeMaximumScaleFactor(const D2D1_MATRIX_3X2_F *matrix);

/*
 * Direct2D Path Geometry, Elliptical Arc, and True Cubic Bezier APIs
 */
void WINAPI d2d_factory_destroy(D2D1_FACTORY *factory);

HRESULT WINAPI d2d_path_geometry_create(
    D2D1_FACTORY *factory,
    D2D1_PATH_GEOMETRY **out_geom);

void WINAPI d2d_path_geometry_destroy(D2D1_PATH_GEOMETRY *geom);

HRESULT WINAPI d2d_path_geometry_begin_figure(
    D2D1_PATH_GEOMETRY *geom,
    D2D1_POINT_2F start_point,
    D2D1_FIGURE_BEGIN figure_begin);

HRESULT WINAPI d2d_path_geometry_add_line(
    D2D1_PATH_GEOMETRY *geom,
    D2D1_POINT_2F point);

HRESULT WINAPI d2d_path_geometry_add_bezier(
    D2D1_PATH_GEOMETRY *geom,
    const D2D1_BEZIER_SEGMENT *bezier);

HRESULT WINAPI d2d_path_geometry_add_quadratic_bezier(
    D2D1_PATH_GEOMETRY *geom,
    const D2D1_QUADRATIC_BEZIER_SEGMENT *bezier);

HRESULT WINAPI d2d_path_geometry_add_arc(
    D2D1_PATH_GEOMETRY *geom,
    const D2D1_ARC_SEGMENT *arc);

HRESULT WINAPI d2d_path_geometry_end_figure(
    D2D1_PATH_GEOMETRY *geom,
    D2D1_FIGURE_END figure_end);

HRESULT WINAPI d2d_path_geometry_close(D2D1_PATH_GEOMETRY *geom);

HRESULT WINAPI d2d_path_geometry_get_bounds(
    const D2D1_PATH_GEOMETRY *geom,
    const D2D1_MATRIX_3X2_F *transform,
    D2D1_RECT_F *bounds);

HRESULT WINAPI d2d_path_geometry_compute_length(
    const D2D1_PATH_GEOMETRY *geom,
    const D2D1_MATRIX_3X2_F *transform,
    float flattening_tolerance,
    float *length);

HRESULT WINAPI d2d_path_geometry_compute_point_at_length(
    const D2D1_PATH_GEOMETRY *geom,
    float length,
    const D2D1_MATRIX_3X2_F *transform,
    float flattening_tolerance,
    D2D1_POINT_2F *point,
    D2D1_POINT_2F *unit_tangent);

HRESULT WINAPI d2d_path_geometry_compute_area(
    const D2D1_PATH_GEOMETRY *geom,
    const D2D1_MATRIX_3X2_F *transform,
    float flattening_tolerance,
    float *area);

HRESULT WINAPI d2d_path_geometry_fill_contains_point(
    const D2D1_PATH_GEOMETRY *geom,
    D2D1_POINT_2F point,
    const D2D1_MATRIX_3X2_F *transform,
    float flattening_tolerance,
    BOOL *contains);

HRESULT WINAPI d2d_path_geometry_stroke_contains_point(
    const D2D1_PATH_GEOMETRY *geom,
    D2D1_POINT_2F point,
    float stroke_width,
    const D2D1_MATRIX_3X2_F *transform,
    float flattening_tolerance,
    BOOL *contains);

uint32_t WINAPI d2d_path_geometry_get_bezier_count(const D2D1_PATH_GEOMETRY *geom);

/*
 * Direct2D Gradient Stop Collection & Brush APIs (Solid, Linear, Radial)
 */
HRESULT WINAPI d2d_gradient_stop_collection_create(
    const D2D1_GRADIENT_STOP *stops,
    uint32_t stop_count,
    D2D1_GAMMA color_interpolation_gamma,
    D2D1_EXTEND_MODE extend_mode,
    D2D1_GRADIENT_STOP_COLLECTION **out_collection);

void WINAPI d2d_gradient_stop_collection_destroy(D2D1_GRADIENT_STOP_COLLECTION *collection);

HRESULT WINAPI d2d_solid_color_brush_create(
    const D2D1_COLOR_F *color,
    float opacity,
    D2D1_BRUSH **out_brush);

HRESULT WINAPI d2d_linear_gradient_brush_create(
    const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES *props,
    float opacity,
    D2D1_GRADIENT_STOP_COLLECTION *stops,
    D2D1_BRUSH **out_brush);

HRESULT WINAPI d2d_radial_gradient_brush_create(
    const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES *props,
    float opacity,
    D2D1_GRADIENT_STOP_COLLECTION *stops,
    D2D1_BRUSH **out_brush);

void WINAPI d2d_brush_destroy(D2D1_BRUSH *brush);

HRESULT WINAPI d2d_gradient_brush_sample(
    const D2D1_BRUSH *brush,
    D2D1_POINT_2F pt,
    D2D1_COLOR_F *out_color);

/*
 * Direct2D Layer & Anti-Aliased Offscreen Render Target APIs
 */
HRESULT WINAPI d2d_layer_create(
    D2D1_SIZE_F size,
    D2D1_LAYER **out_layer);

void WINAPI d2d_layer_destroy(D2D1_LAYER *layer);

HRESULT WINAPI d2d_render_target_create(
    uint32_t width,
    uint32_t height,
    D2D1_RENDER_TARGET **out_rt);

void WINAPI d2d_render_target_destroy(D2D1_RENDER_TARGET *rt);

void WINAPI d2d_render_target_begin_draw(D2D1_RENDER_TARGET *rt);
HRESULT WINAPI d2d_render_target_end_draw(D2D1_RENDER_TARGET *rt);

void WINAPI d2d_render_target_clear(
    D2D1_RENDER_TARGET *rt,
    const D2D1_COLOR_F *clear_color);

void WINAPI d2d_render_target_set_antialias_mode(
    D2D1_RENDER_TARGET *rt,
    D2D1_ANTIALIAS_MODE antialias_mode);

D2D1_ANTIALIAS_MODE WINAPI d2d_render_target_get_antialias_mode(
    const D2D1_RENDER_TARGET *rt);

void WINAPI d2d_render_target_set_transform(
    D2D1_RENDER_TARGET *rt,
    const D2D1_MATRIX_3X2_F *transform);

HRESULT WINAPI d2d_render_target_push_layer(
    D2D1_RENDER_TARGET *rt,
    const D2D1_LAYER_PARAMETERS *layer_parameters,
    D2D1_LAYER *layer);

HRESULT WINAPI d2d_render_target_pop_layer(D2D1_RENDER_TARGET *rt);

HRESULT WINAPI d2d_render_target_fill_rectangle(
    D2D1_RENDER_TARGET *rt,
    const D2D1_RECT_F *rect,
    const D2D1_BRUSH *brush);

HRESULT WINAPI d2d_render_target_fill_geometry(
    D2D1_RENDER_TARGET *rt,
    const D2D1_PATH_GEOMETRY *geometry,
    const D2D1_BRUSH *brush,
    const D2D1_BRUSH *opacity_brush);

HRESULT WINAPI d2d_render_target_draw_geometry(
    D2D1_RENDER_TARGET *rt,
    const D2D1_PATH_GEOMETRY *geometry,
    const D2D1_BRUSH *brush,
    float stroke_width);

HRESULT WINAPI d2d_render_target_draw_line(
    D2D1_RENDER_TARGET *rt,
    D2D1_POINT_2F p0,
    D2D1_POINT_2F p1,
    const D2D1_BRUSH *brush,
    float stroke_width);

HRESULT WINAPI d2d_render_target_read_pixel(
    const D2D1_RENDER_TARGET *rt,
    uint32_t x,
    uint32_t y,
    D2D1_COLOR_F *out_color);

/*
 * vkd3d-shader Metal Shading Language (MSL) Generator & Metal GPU Compiler
 */
typedef enum VKD3D_SHADER_TYPE {
    VKD3D_SHADER_TYPE_PIXEL = 0,
    VKD3D_SHADER_TYPE_VERTEX = 1,
    VKD3D_SHADER_TYPE_COMPUTE = 2
} VKD3D_SHADER_TYPE;

#define VKD3D_SHADER_COMPILE_FLAG_NONE              0x00000000u
#define VKD3D_SHADER_COMPILE_FLAG_ZERO_INF_MUL      0x00000001u
#define VKD3D_SHADER_COMPILE_FLAG_VERIFY_METAL_GPU  0x00000002u

typedef struct VKD3D_SHADER_SOURCE_DESC {
    const char *hlsl_ir_text;
    const char *entry_point;
    VKD3D_SHADER_TYPE shader_type;
    uint32_t flags;
} VKD3D_SHADER_SOURCE_DESC;

typedef struct VKD3D_SHADER_MSL_OUTPUT {
    char msl_source[8192];
    size_t msl_length;
    uint32_t input_count;
    uint32_t output_count;
    uint32_t cbuffer_count;
    uint32_t texture_count;
    uint32_t sampler_count;
    uint32_t instruction_count;
    BOOL metal_library_compiled;
    char metal_device_name[128];
    char error_log[512];
} VKD3D_SHADER_MSL_OUTPUT;

HRESULT WINAPI vkd3d_shader_compile_msl(
    const VKD3D_SHADER_SOURCE_DESC *desc,
    VKD3D_SHADER_MSL_OUTPUT *out_msl);

#ifdef __cplusplus
}
#endif

#endif /* _WINE_D2D1_PARITY_H */
