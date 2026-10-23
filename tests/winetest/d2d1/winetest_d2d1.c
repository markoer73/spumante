/*
 * Upstream Wine winetest anti-regression suite for d2d1.dll & vkd3d-shader MSL
 * (Direct2D Factory/Matrix Exports, True Cubic Beziers, Elliptical Arcs,
 *  PushLayer/PopLayer Stack, Supersampled Anti-Aliasing, Gradient Brushes,
 *  and vkd3d-shader Metal Shading Language Generator + MTLLibrary Compiler)
 *
 * Adapted from upstream Wine dlls/d2d1/tests/d2d1.c
 * Copyright (C) 2014-2024 Wine contributors
 * Copyright (C) 2026 Wine Parity Project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <math.h>
#include <string.h>
#include "../../include/wine/test.h"
#include "../../../dlls/d2d1/d2d1.h"

static int wt_near(float a, float b, float eps)
{
    return fabsf(a - b) <= eps;
}

static void test_factory_and_matrix_exports(void)
{
    D2D1_FACTORY *factory = NULL;
    D2D1_FACTORY_OPTIONS opts;
    D2D1_MATRIX_3X2_F m, skew;
    D2D1_POINT_2F center = {15.0f, 25.0f};
    D2D1_COLOR_F srgb = {0.5f, 0.25f, 0.75f, 1.0f};
    D2D1_COLOR_F linear_col, roundtrip_col;
    void *dev = NULL, *ctx = NULL;
    int dummy_dxgi = 1;
    float s = 0.0f, c = 0.0f, t = 0.0f, len = 0.0f, scale = 0.0f;
    HRESULT hr;

    hr = D2D1CreateFactory((D2D1_FACTORY_TYPE)2, NULL, NULL, (void **)&factory);
    ok(hr == E_INVALIDARG && factory == NULL, "Expected E_INVALIDARG, got 0x%08x\n", (unsigned)hr);

    opts.debugLevel = (D2D1_DEBUG_LEVEL)99;
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, NULL, &opts, (void **)&factory);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG for invalid debugLevel, got 0x%08x\n", (unsigned)hr);

    opts.debugLevel = D2D1_DEBUG_LEVEL_NONE;
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, NULL, &opts, (void **)&factory);
    ok(hr == S_OK && factory != NULL, "D2D1CreateFactory failed: 0x%08x\n", (unsigned)hr);

    D2D1MakeRotateMatrix(90.0f, center, &m);
    ok(D2D1IsMatrixInvertible(&m), "90-deg rotation matrix should be invertible\n");
    scale = D2D1ComputeMaximumScaleFactor(&m);
    ok(wt_near(scale, 1.0f, 1e-3f), "Expected scale factor 1.0, got %f\n", scale);
    ok(D2D1InvertMatrix(&m), "D2D1InvertMatrix failed\n");

    D2D1MakeSkewMatrix(45.0f, 0.0f, center, &skew);
    ok(wt_near(skew._21, 1.0f, 1e-3f), "Expected skew _21=1.0, got %f\n", skew._21);

    D2D1SinCos(0.0f, &s, &c);
    t = D2D1Tan(0.0f);
    len = D2D1Vec3Length(3.0f, 4.0f, 12.0f);
    ok(wt_near(s, 0.0f, 1e-4f) && wt_near(c, 1.0f, 1e-4f) && wt_near(t, 0.0f, 1e-4f),
       "Trig helpers failed\n");
    ok(wt_near(len, 13.0f, 1e-4f), "Expected Vec3Length=13, got %f\n", len);

    linear_col = D2D1ConvertColorSpace(D2D1_COLOR_SPACE_SRGB, D2D1_COLOR_SPACE_SCRGB, &srgb);
    roundtrip_col = D2D1ConvertColorSpace(D2D1_COLOR_SPACE_SCRGB, D2D1_COLOR_SPACE_SRGB, &linear_col);
    ok(wt_near(roundtrip_col.r, srgb.r, 1e-3f) && wt_near(roundtrip_col.g, srgb.g, 1e-3f),
       "Color space roundtrip mismatch\n");

    hr = D2D1CreateDevice(&dummy_dxgi, NULL, &dev);
    ok(hr == S_OK && dev != NULL, "D2D1CreateDevice failed: 0x%08x\n", (unsigned)hr);
    d2d_factory_destroy((D2D1_FACTORY *)dev);

    hr = D2D1CreateDeviceContext(&dummy_dxgi, NULL, &ctx);
    ok(hr == S_OK && ctx != NULL, "D2D1CreateDeviceContext failed: 0x%08x\n", (unsigned)hr);
    d2d_render_target_destroy((D2D1_RENDER_TARGET *)ctx);

    d2d_factory_destroy(factory);
}

static void test_true_cubic_bezier(void)
{
    D2D1_FACTORY *factory = NULL;
    D2D1_PATH_GEOMETRY *geom = NULL;
    D2D1_POINT_2F p0 = {0.0f, 50.0f};
    D2D1_BEZIER_SEGMENT bez = {{25.0f, 0.0f}, {75.0f, 100.0f}, {100.0f, 50.0f}};
    D2D1_RECT_F bounds = {0};
    float len = 0.0f;
    D2D1_POINT_2F mid = {0}, tan_v = {0};
    HRESULT hr;

    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, NULL, NULL, (void **)&factory);
    ok(hr == S_OK, "CreateFactory failed\n");

    hr = d2d_path_geometry_create(factory, &geom);
    ok(hr == S_OK && geom != NULL, "path_geometry_create failed\n");

    hr = d2d_path_geometry_begin_figure(geom, p0, D2D1_FIGURE_BEGIN_HOLLOW);
    ok(hr == S_OK, "begin_figure failed\n");
    hr = d2d_path_geometry_add_bezier(geom, &bez);
    ok(hr == S_OK, "add_bezier failed\n");
    hr = d2d_path_geometry_end_figure(geom, D2D1_FIGURE_END_OPEN);
    ok(hr == S_OK, "end_figure failed\n");
    hr = d2d_path_geometry_close(geom);
    ok(hr == S_OK, "close failed\n");

    hr = d2d_path_geometry_get_bounds(geom, NULL, &bounds);
    ok(hr == S_OK, "get_bounds failed\n");
    todo_wine ok(wt_near(bounds.top, 35.566f, 0.5f) && wt_near(bounds.bottom, 64.434f, 0.5f),
                 "Expected exact cubic S-curve y-extrema [35.566, 64.434], got [%f, %f]\n",
                 bounds.top, bounds.bottom);

    hr = d2d_path_geometry_compute_length(geom, NULL, D2D1_DEFAULT_FLATTENING_TOLERANCE, &len);
    ok(hr == S_OK, "compute_length failed\n");
    todo_wine ok(len > 114.0f && len < 120.0f, "Expected true cubic arc length ~116.86, got %f\n", len);

    hr = d2d_path_geometry_compute_point_at_length(
        geom, len * 0.5f, NULL, D2D1_DEFAULT_FLATTENING_TOLERANCE, &mid, &tan_v);
    ok(hr == S_OK && wt_near(mid.x, 50.0f, 1.0f) && wt_near(mid.y, 50.0f, 1.0f),
       "Midpoint mismatch (%f, %f)\n", mid.x, mid.y);

    d2d_path_geometry_destroy(geom);
    d2d_factory_destroy(factory);
}

static void test_elliptical_arc(void)
{
    D2D1_FACTORY *factory = NULL;
    D2D1_PATH_GEOMETRY *geom = NULL;
    D2D1_RENDER_TARGET *rt = NULL;
    D2D1_BRUSH *brush = NULL;
    D2D1_COLOR_F black = {0.0f, 0.0f, 0.0f, 1.0f};
    D2D1_COLOR_F green = {0.0f, 1.0f, 0.0f, 1.0f};
    D2D1_POINT_2F start_pt = {10.0f, 50.0f};
    D2D1_ARC_SEGMENT arc = {{90.0f, 50.0f}, {40.0f, 40.0f}, 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL};
    D2D1_RECT_F bounds = {0};
    float area = 0.0f;
    D2D1_COLOR_F px = {0};
    HRESULT hr;

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, NULL, NULL, (void **)&factory);
    d2d_path_geometry_create(factory, &geom);
    d2d_path_geometry_begin_figure(geom, start_pt, D2D1_FIGURE_BEGIN_FILLED);
    hr = d2d_path_geometry_add_arc(geom, &arc);
    ok(hr == S_OK, "add_arc failed: 0x%08x\n", (unsigned)hr);
    d2d_path_geometry_end_figure(geom, D2D1_FIGURE_END_CLOSED);
    d2d_path_geometry_close(geom);

    d2d_path_geometry_get_bounds(geom, NULL, &bounds);
    d2d_path_geometry_compute_area(geom, NULL, D2D1_DEFAULT_FLATTENING_TOLERANCE, &area);
    todo_wine ok(wt_near(bounds.top, 10.0f, 0.5f) && wt_near(area, 2513.27f, 25.0f),
                 "Arc semicircle bounds/area mismatch: top=%f area=%f\n", bounds.top, area);

    d2d_render_target_create(100, 100, &rt);
    d2d_solid_color_brush_create(&green, 1.0f, &brush);
    d2d_render_target_begin_draw(rt);
    d2d_render_target_clear(rt, &black);
    hr = d2d_render_target_draw_geometry(rt, geom, brush, 4.0f);
    ok(hr == S_OK, "draw_geometry(arc) failed\n");
    d2d_render_target_read_pixel(rt, 50, 10, &px);
    todo_wine ok(px.g > 0.7f, "Expected green arc stroke pixel at (50,10), got g=%f\n", px.g);
    d2d_render_target_end_draw(rt);

    d2d_brush_destroy(brush);
    d2d_render_target_destroy(rt);
    d2d_path_geometry_destroy(geom);
    d2d_factory_destroy(factory);
}

static void test_layers_and_antialiasing(void)
{
    D2D1_FACTORY *factory = NULL;
    D2D1_PATH_GEOMETRY *tri = NULL;
    D2D1_RENDER_TARGET *rt = NULL;
    D2D1_LAYER *layer = NULL;
    D2D1_BRUSH *red_brush = NULL;
    D2D1_COLOR_F black = {0.0f, 0.0f, 0.0f, 1.0f};
    D2D1_COLOR_F red = {1.0f, 0.0f, 0.0f, 1.0f};
    D2D1_SIZE_F sz = {32.0f, 32.0f};
    D2D1_LAYER_PARAMETERS lp;
    D2D1_RECT_F full = {0.0f, 0.0f, 32.0f, 32.0f};
    D2D1_COLOR_F px_in = {0}, px_out = {0};
    uint32_t aa_partial = 0, aliased_partial = 0, x, y;
    HRESULT hr;

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, NULL, NULL, (void **)&factory);
    d2d_render_target_create(32, 32, &rt);
    d2d_layer_create(sz, &layer);
    d2d_solid_color_brush_create(&red, 1.0f, &red_brush);

    memset(&lp, 0, sizeof(lp));
    lp.contentBounds.left = 8.0f;
    lp.contentBounds.top = 8.0f;
    lp.contentBounds.right = 24.0f;
    lp.contentBounds.bottom = 24.0f;
    lp.maskTransform._11 = 1.0f;
    lp.maskTransform._22 = 1.0f;
    lp.opacity = 0.5f;

    d2d_render_target_begin_draw(rt);
    d2d_render_target_clear(rt, &black);
    hr = d2d_render_target_push_layer(rt, &lp, layer);
    ok(hr == S_OK, "PushLayer failed\n");
    d2d_render_target_fill_rectangle(rt, &full, red_brush);
    hr = d2d_render_target_pop_layer(rt);
    ok(hr == S_OK, "PopLayer failed\n");
    hr = d2d_render_target_end_draw(rt);
    ok(hr == S_OK, "EndDraw failed\n");

    d2d_render_target_read_pixel(rt, 16, 16, &px_in);
    d2d_render_target_read_pixel(rt, 2, 2, &px_out);
    todo_wine ok(wt_near(px_in.r, 0.5f, 0.05f) && wt_near(px_out.r, 0.0f, 0.02f),
                 "Layer opacity/bounds compositing mismatch: in=%f out=%f\n", px_in.r, px_out.r);

    d2d_path_geometry_create(factory, &tri);
    {
        D2D1_POINT_2F p0 = {0.25f, 0.25f}, p1 = {31.75f, 16.25f}, p2 = {0.25f, 31.75f};
        d2d_path_geometry_begin_figure(tri, p0, D2D1_FIGURE_BEGIN_FILLED);
        d2d_path_geometry_add_line(tri, p1);
        d2d_path_geometry_add_line(tri, p2);
        d2d_path_geometry_end_figure(tri, D2D1_FIGURE_END_CLOSED);
        d2d_path_geometry_close(tri);
    }

    d2d_render_target_begin_draw(rt);
    d2d_render_target_clear(rt, &black);
    d2d_render_target_set_antialias_mode(rt, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    d2d_render_target_fill_geometry(rt, tri, red_brush, NULL);
    d2d_render_target_end_draw(rt);
    for (y = 0; y < 32; ++y) {
        for (x = 0; x < 32; ++x) {
            D2D1_COLOR_F c = {0};
            d2d_render_target_read_pixel(rt, x, y, &c);
            if (c.r > 0.05f && c.r < 0.95f) aa_partial++;
        }
    }

    d2d_render_target_begin_draw(rt);
    d2d_render_target_clear(rt, &black);
    d2d_render_target_set_antialias_mode(rt, D2D1_ANTIALIAS_MODE_ALIASED);
    d2d_render_target_fill_geometry(rt, tri, red_brush, NULL);
    d2d_render_target_end_draw(rt);
    for (y = 0; y < 32; ++y) {
        for (x = 0; x < 32; ++x) {
            D2D1_COLOR_F c = {0};
            d2d_render_target_read_pixel(rt, x, y, &c);
            if (c.r > 0.05f && c.r < 0.95f) aliased_partial++;
        }
    }
    todo_wine ok(aa_partial >= 10 && aliased_partial == 0,
                 "Expected supersampled edge coverage in PER_PRIMITIVE (%u) vs ALIASED (%u)\n",
                 aa_partial, aliased_partial);

    d2d_path_geometry_destroy(tri);
    d2d_brush_destroy(red_brush);
    d2d_layer_destroy(layer);
    d2d_render_target_destroy(rt);
    d2d_factory_destroy(factory);
}

static void test_gradient_brushes(void)
{
    D2D1_GRADIENT_STOP stops[2] = {
        {0.0f, {0.0f, 0.0f, 0.0f, 1.0f}},
        {1.0f, {1.0f, 1.0f, 1.0f, 1.0f}}
    };
    D2D1_GRADIENT_STOP_COLLECTION *sc_mirror = NULL;
    D2D1_BRUSH *linear_b = NULL;
    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES lprops = {{0.0f, 0.0f}, {100.0f, 0.0f}};
    D2D1_POINT_2F pt = {125.0f, 0.0f};
    D2D1_COLOR_F col = {0};
    HRESULT hr;

    hr = d2d_gradient_stop_collection_create(stops, 2, D2D1_GAMMA_1_0, D2D1_EXTEND_MODE_MIRROR, &sc_mirror);
    ok(hr == S_OK && sc_mirror != NULL, "gradient_stop_collection_create failed\n");
    hr = d2d_linear_gradient_brush_create(&lprops, 1.0f, sc_mirror, &linear_b);
    ok(hr == S_OK && linear_b != NULL, "linear_gradient_brush_create failed\n");

    hr = d2d_gradient_brush_sample(linear_b, pt, &col);
    todo_wine ok(hr == S_OK && col.r > 0.70f && col.r < 0.95f,
                 "Expected MIRROR + GAMMA_1_0 gradient sample in (0.70, 0.95), got %f\n", col.r);

    d2d_brush_destroy(linear_b);
    d2d_gradient_stop_collection_destroy(sc_mirror);
}

static void test_vkd3d_shader_msl(void)
{
    VKD3D_SHADER_SOURCE_DESC desc;
    VKD3D_SHADER_MSL_OUTPUT out_msl;
    const char *vs_ir =
        "vs_5_0\n"
        "dcl_constant_buffer cb0[4], immediateIndexed\n"
        "dcl_input v0.xyzw\n"
        "dcl_output o0.xyzw\n"
        "dcl_temps 1\n"
        "mul r0, v0, cb0[0]\n"
        "add o0, r0, cb0[1]\n"
        "ret\n";
    HRESULT hr;

    memset(&desc, 0, sizeof(desc));
    memset(&out_msl, 0, sizeof(out_msl));
    desc.hlsl_ir_text = vs_ir;
    desc.entry_point = "vs_main";
    desc.shader_type = VKD3D_SHADER_TYPE_VERTEX;
    desc.flags = VKD3D_SHADER_COMPILE_FLAG_ZERO_INF_MUL | VKD3D_SHADER_COMPILE_FLAG_VERIFY_METAL_GPU;

    hr = vkd3d_shader_compile_msl(&desc, &out_msl);
    ok(hr == S_OK, "vkd3d_shader_compile_msl failed: 0x%08x\n", (unsigned)hr);
    todo_wine ok(out_msl.metal_library_compiled && strstr(out_msl.msl_source, "vertex vkd3d_out_t vs_main") != NULL,
                 "Expected MSL vertex shader compiled into MTLLibrary\n");
}

START_TEST(d2d1)
{
    WINETEST_RUN_SUBTEST(test_factory_and_matrix_exports);
    WINETEST_RUN_SUBTEST(test_true_cubic_bezier);
    WINETEST_RUN_SUBTEST(test_elliptical_arc);
    WINETEST_RUN_SUBTEST(test_layers_and_antialiasing);
    WINETEST_RUN_SUBTEST(test_gradient_brushes);
    WINETEST_RUN_SUBTEST(test_vkd3d_shader_msl);
}
