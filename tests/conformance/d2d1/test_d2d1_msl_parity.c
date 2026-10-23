/*
 * Differential Conformance Test: Direct2D Primitives & vkd3d-shader MSL Generator
 *
 * Clean-Room Spec: specs/d2d1/
 * Copyright (C) 2026 Wine Parity Project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <stddef.h>
#include <math.h>
#include <string.h>
#include "../../include/parity_test.h"
#include "../../../dlls/d2d1/d2d1.h"

static int float_near(float a, float b, float eps)
{
    return fabsf(a - b) <= eps;
}

int main(void)
{
    parity_begin_document("d2d1", "test_d2d1_msl_parity", "x86_64", "wine-parity");

    /* 1. D2D1CreateFactory_InvalidType */
    {
        void *factory = NULL;
        HRESULT hr = D2D1CreateFactory((D2D1_FACTORY_TYPE)99, NULL, NULL, &factory);
        parity_record_exact(
            "d2d1_001",
            "D2D1CreateFactory_InvalidType",
            "D2D1CreateFactory",
            hr,
            0,
            0,
            NULL,
            0,
            factory != NULL ? 1 : 0);
    }

    /* 2. D2D1CreateFactory_And_MatrixMath */
    {
        D2D1_FACTORY *factory = NULL;
        D2D1_MATRIX_3X2_F rot;
        D2D1_POINT_2F center = {10.0f, 20.0f};
        float s = 0.0f, c = 0.0f;
        float len = 0.0f;
        BOOL inv_ok = FALSE;
        HRESULT hr = D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED,
            NULL,
            NULL,
            (void **)&factory);

        memset(&rot, 0, sizeof(rot));
        D2D1MakeRotateMatrix(90.0f, center, &rot);
        inv_ok = D2D1IsMatrixInvertible(&rot) && D2D1InvertMatrix(&rot);
        D2D1SinCos(0.0f, &s, &c);
        len = D2D1Vec3Length(3.0f, 4.0f, 12.0f);

        int ok = (hr == S_OK && factory != NULL && inv_ok &&
                  float_near(s, 0.0f, 1e-4f) && float_near(c, 1.0f, 1e-4f) &&
                  float_near(len, 13.0f, 1e-4f));
        parity_record_exact(
            "d2d1_002",
            "D2D1CreateFactory_And_MatrixMath",
            "D2D1CreateFactory",
            hr,
            0,
            0,
            NULL,
            0,
            ok ? 1 : 0);
        if (factory) {
            d2d_factory_destroy(factory);
        }
    }

    /* 3. TrueCubicBezier_ExactInflectionBoundsAndLength */
    {
        D2D1_FACTORY *factory = NULL;
        D2D1_PATH_GEOMETRY *geom = NULL;
        D2D1_POINT_2F p0 = {0.0f, 50.0f};
        D2D1_BEZIER_SEGMENT bez = {
            {25.0f, 0.0f},
            {75.0f, 100.0f},
            {100.0f, 50.0f}
        };
        D2D1_RECT_F bounds = {0.0f, 0.0f, 0.0f, 0.0f};
        float length = 0.0f;
        D2D1_POINT_2F mid_pt = {0.0f, 0.0f};
        D2D1_POINT_2F mid_tan = {0.0f, 0.0f};
        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, NULL, NULL, (void **)&factory);
        if (hr == S_OK) hr = d2d_path_geometry_create(factory, &geom);
        if (hr == S_OK) hr = d2d_path_geometry_begin_figure(geom, p0, D2D1_FIGURE_BEGIN_HOLLOW);
        if (hr == S_OK) hr = d2d_path_geometry_add_bezier(geom, &bez);
        if (hr == S_OK) hr = d2d_path_geometry_end_figure(geom, D2D1_FIGURE_END_OPEN);
        if (hr == S_OK) hr = d2d_path_geometry_close(geom);
        if (hr == S_OK) hr = d2d_path_geometry_get_bounds(geom, NULL, &bounds);
        if (hr == S_OK) hr = d2d_path_geometry_compute_length(geom, NULL, D2D1_DEFAULT_FLATTENING_TOLERANCE, &length);
        if (hr == S_OK) {
            hr = d2d_path_geometry_compute_point_at_length(
                geom,
                length * 0.5f,
                NULL,
                D2D1_DEFAULT_FLATTENING_TOLERANCE,
                &mid_pt,
                &mid_tan);
        }

        /*
         * For S-curve P0=(0,50), P1=(25,0), P2=(75,100), P3=(100,50):
         * Analytical y-extrema occur at t = (3 -/+ sqrt(3))/6:
         * y_min = 50 - 25/sqrt(3) = 35.5662f, y_max = 50 + 25/sqrt(3) = 64.4338f,
         * and true arc length ~ 116.86f.
         * Upstream's single quadratic approximation collapses y to 50.0 everywhere (length=100)!
         */
        int ok = (hr == S_OK &&
                  float_near(bounds.left, 0.0f, 0.5f) &&
                  float_near(bounds.right, 100.0f, 0.5f) &&
                  float_near(bounds.top, 35.566f, 0.5f) &&
                  float_near(bounds.bottom, 64.434f, 0.5f) &&
                  length > 114.0f && length < 120.0f &&
                  float_near(mid_pt.x, 50.0f, 1.0f) &&
                  float_near(mid_pt.y, 50.0f, 1.0f));
        parity_record_exact(
            "d2d1_003",
            "TrueCubicBezier_ExactInflectionBoundsAndLength",
            "d2d_path_geometry_add_bezier",
            hr,
            0,
            0,
            NULL,
            0,
            ok ? 1 : 0);
        if (geom) d2d_path_geometry_destroy(geom);
        if (factory) d2d_factory_destroy(factory);
    }

    /* 4. EllipticalArc_EndpointToBezierAndArea */
    {
        D2D1_FACTORY *factory = NULL;
        D2D1_PATH_GEOMETRY *geom = NULL;
        D2D1_POINT_2F start_pt = {10.0f, 50.0f};
        D2D1_ARC_SEGMENT arc;
        D2D1_RECT_F bounds = {0.0f, 0.0f, 0.0f, 0.0f};
        float length = 0.0f;
        float area = 0.0f;
        BOOL inside = FALSE, outside = TRUE;

        arc.point.x = 90.0f;
        arc.point.y = 50.0f;
        arc.size.width = 40.0f;
        arc.size.height = 40.0f;
        arc.rotationAngle = 0.0f;
        arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
        arc.arcSize = D2D1_ARC_SIZE_SMALL;

        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, NULL, NULL, (void **)&factory);
        if (hr == S_OK) hr = d2d_path_geometry_create(factory, &geom);
        if (hr == S_OK) hr = d2d_path_geometry_begin_figure(geom, start_pt, D2D1_FIGURE_BEGIN_FILLED);
        if (hr == S_OK) hr = d2d_path_geometry_add_arc(geom, &arc);
        if (hr == S_OK) hr = d2d_path_geometry_end_figure(geom, D2D1_FIGURE_END_CLOSED);
        if (hr == S_OK) hr = d2d_path_geometry_close(geom);
        if (hr == S_OK) hr = d2d_path_geometry_get_bounds(geom, NULL, &bounds);
        if (hr == S_OK) hr = d2d_path_geometry_compute_length(geom, NULL, D2D1_DEFAULT_FLATTENING_TOLERANCE, &length);
        if (hr == S_OK) hr = d2d_path_geometry_compute_area(geom, NULL, D2D1_DEFAULT_FLATTENING_TOLERANCE, &area);
        if (hr == S_OK) {
            D2D1_POINT_2F pt_in = {50.0f, 25.0f};
            D2D1_POINT_2F pt_out = {50.0f, 75.0f};
            d2d_path_geometry_fill_contains_point(geom, pt_in, NULL, D2D1_DEFAULT_FLATTENING_TOLERANCE, &inside);
            d2d_path_geometry_fill_contains_point(geom, pt_out, NULL, D2D1_DEFAULT_FLATTENING_TOLERANCE, &outside);
        }

        int ok = (hr == S_OK &&
                  d2d_path_geometry_get_bezier_count(geom) == 2 &&
                  float_near(bounds.left, 10.0f, 0.5f) &&
                  float_near(bounds.top, 10.0f, 0.5f) &&
                  float_near(bounds.right, 90.0f, 0.5f) &&
                  float_near(bounds.bottom, 50.0f, 0.5f) &&
                  float_near(length, 205.66f, 1.5f) &&
                  float_near(area, 2513.27f, 25.0f) &&
                  inside == TRUE && outside == FALSE);
        parity_record_exact(
            "d2d1_004",
            "EllipticalArc_EndpointToBezierAndArea",
            "d2d_path_geometry_add_arc",
            hr,
            0,
            0,
            NULL,
            0,
            ok ? 1 : 0);
        if (geom) d2d_path_geometry_destroy(geom);
        if (factory) d2d_factory_destroy(factory);
    }

    /* 5. EllipticalArc_RasterizeStrokeAndFill */
    {
        D2D1_FACTORY *factory = NULL;
        D2D1_PATH_GEOMETRY *geom = NULL;
        D2D1_RENDER_TARGET *rt = NULL;
        D2D1_BRUSH *brush = NULL;
        D2D1_COLOR_F black = {0.0f, 0.0f, 0.0f, 1.0f};
        D2D1_COLOR_F green = {0.0f, 1.0f, 0.0f, 1.0f};
        D2D1_POINT_2F start_pt = {10.0f, 32.0f};
        D2D1_ARC_SEGMENT arc = {{54.0f, 32.0f}, {22.0f, 22.0f}, 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL};
        D2D1_COLOR_F px_stroke = {0.0f, 0.0f, 0.0f, 0.0f};
        D2D1_COLOR_F px_center = {0.0f, 0.0f, 0.0f, 0.0f};

        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, NULL, NULL, (void **)&factory);
        if (hr == S_OK) hr = d2d_path_geometry_create(factory, &geom);
        if (hr == S_OK) hr = d2d_path_geometry_begin_figure(geom, start_pt, D2D1_FIGURE_BEGIN_FILLED);
        if (hr == S_OK) hr = d2d_path_geometry_add_arc(geom, &arc);
        if (hr == S_OK) hr = d2d_path_geometry_end_figure(geom, D2D1_FIGURE_END_CLOSED);
        if (hr == S_OK) hr = d2d_path_geometry_close(geom);
        if (hr == S_OK) hr = d2d_render_target_create(64, 64, &rt);
        if (hr == S_OK) hr = d2d_solid_color_brush_create(&green, 1.0f, &brush);
        if (hr == S_OK) {
            d2d_render_target_begin_draw(rt);
            d2d_render_target_clear(rt, &black);
            hr = d2d_render_target_draw_geometry(rt, geom, brush, 4.0f);
            if (hr == S_OK) {
                hr = d2d_render_target_read_pixel(rt, 32, 10, &px_stroke);
            }
            if (hr == S_OK) {
                hr = d2d_render_target_fill_geometry(rt, geom, brush, NULL);
            }
            if (hr == S_OK) {
                hr = d2d_render_target_read_pixel(rt, 32, 20, &px_center);
            }
            if (hr == S_OK) {
                hr = d2d_render_target_end_draw(rt);
            }
        }

        int ok = (hr == S_OK && px_stroke.g > 0.7f && px_center.g > 0.9f);
        parity_record_exact(
            "d2d1_005",
            "EllipticalArc_RasterizeStrokeAndFill",
            "d2d_path_geometry_add_arc",
            hr,
            0,
            0,
            NULL,
            0,
            ok ? 1 : 0);
        if (brush) d2d_brush_destroy(brush);
        if (rt) d2d_render_target_destroy(rt);
        if (geom) d2d_path_geometry_destroy(geom);
        if (factory) d2d_factory_destroy(factory);
    }

    /* 6. LayerPushPop_OpacityAndBoundsClipping */
    {
        D2D1_RENDER_TARGET *rt = NULL;
        D2D1_LAYER *layer = NULL;
        D2D1_BRUSH *red_brush = NULL;
        D2D1_COLOR_F black = {0.0f, 0.0f, 0.0f, 1.0f};
        D2D1_COLOR_F red = {1.0f, 0.0f, 0.0f, 1.0f};
        D2D1_SIZE_F layer_sz = {64.0f, 64.0f};
        D2D1_LAYER_PARAMETERS lp;
        D2D1_RECT_F full_rect = {0.0f, 0.0f, 64.0f, 64.0f};
        D2D1_COLOR_F px_in = {0.0f, 0.0f, 0.0f, 0.0f};
        D2D1_COLOR_F px_out = {0.0f, 0.0f, 0.0f, 0.0f};

        memset(&lp, 0, sizeof(lp));
        lp.contentBounds.left = 10.0f;
        lp.contentBounds.top = 10.0f;
        lp.contentBounds.right = 50.0f;
        lp.contentBounds.bottom = 50.0f;
        lp.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
        lp.maskTransform._11 = 1.0f;
        lp.maskTransform._22 = 1.0f;
        lp.opacity = 0.5f;

        HRESULT hr = d2d_render_target_create(64, 64, &rt);
        if (hr == S_OK) hr = d2d_layer_create(layer_sz, &layer);
        if (hr == S_OK) hr = d2d_solid_color_brush_create(&red, 1.0f, &red_brush);
        if (hr == S_OK) {
            d2d_render_target_begin_draw(rt);
            d2d_render_target_clear(rt, &black);
            hr = d2d_render_target_push_layer(rt, &lp, layer);
            if (hr == S_OK) hr = d2d_render_target_fill_rectangle(rt, &full_rect, red_brush);
            if (hr == S_OK) hr = d2d_render_target_pop_layer(rt);
            if (hr == S_OK) hr = d2d_render_target_end_draw(rt);
            if (hr == S_OK) {
                d2d_render_target_read_pixel(rt, 25, 25, &px_in);
                d2d_render_target_read_pixel(rt, 4, 4, &px_out);
            }
        }

        int ok = (hr == S_OK &&
                  float_near(px_in.r, 0.5f, 0.05f) &&
                  float_near(px_out.r, 0.0f, 0.02f));
        parity_record_exact(
            "d2d1_006",
            "LayerPushPop_OpacityAndBoundsClipping",
            "d2d_render_target_push_pop_layer",
            hr,
            0,
            0,
            NULL,
            0,
            ok ? 1 : 0);
        if (red_brush) d2d_brush_destroy(red_brush);
        if (layer) d2d_layer_destroy(layer);
        if (rt) d2d_render_target_destroy(rt);
    }

    /* 7. LayerPushPop_GeometricMaskAndUnbalancedError */
    {
        D2D1_FACTORY *factory = NULL;
        D2D1_PATH_GEOMETRY *mask_geom = NULL;
        D2D1_RENDER_TARGET *rt = NULL;
        D2D1_BRUSH *blue_brush = NULL;
        D2D1_COLOR_F black = {0.0f, 0.0f, 0.0f, 1.0f};
        D2D1_COLOR_F blue = {0.0f, 0.0f, 1.0f, 1.0f};
        D2D1_LAYER_PARAMETERS lp;
        D2D1_RECT_F full_rect = {0.0f, 0.0f, 64.0f, 64.0f};
        D2D1_COLOR_F px_masked_in = {0.0f, 0.0f, 0.0f, 0.0f};
        D2D1_COLOR_F px_masked_out = {0.0f, 0.0f, 0.0f, 0.0f};
        HRESULT hr_unbalanced = S_OK;
        HRESULT hr_extra_pop = S_OK;

        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, NULL, NULL, (void **)&factory);
        if (hr == S_OK) hr = d2d_path_geometry_create(factory, &mask_geom);
        if (hr == S_OK) {
            D2D1_POINT_2F p0 = {16.0f, 16.0f};
            D2D1_POINT_2F p1 = {48.0f, 16.0f};
            D2D1_POINT_2F p2 = {48.0f, 48.0f};
            D2D1_POINT_2F p3 = {16.0f, 48.0f};
            d2d_path_geometry_begin_figure(mask_geom, p0, D2D1_FIGURE_BEGIN_FILLED);
            d2d_path_geometry_add_line(mask_geom, p1);
            d2d_path_geometry_add_line(mask_geom, p2);
            d2d_path_geometry_add_line(mask_geom, p3);
            d2d_path_geometry_end_figure(mask_geom, D2D1_FIGURE_END_CLOSED);
            d2d_path_geometry_close(mask_geom);
        }

        memset(&lp, 0, sizeof(lp));
        lp.contentBounds = full_rect;
        lp.geometricMask = mask_geom;
        lp.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
        lp.maskTransform._11 = 1.0f;
        lp.maskTransform._22 = 1.0f;
        lp.opacity = 1.0f;

        if (hr == S_OK) hr = d2d_render_target_create(64, 64, &rt);
        if (hr == S_OK) hr = d2d_solid_color_brush_create(&blue, 1.0f, &blue_brush);
        if (hr == S_OK) {
            d2d_render_target_begin_draw(rt);
            d2d_render_target_clear(rt, &black);
            hr = d2d_render_target_push_layer(rt, &lp, NULL);
            if (hr == S_OK) hr = d2d_render_target_fill_rectangle(rt, &full_rect, blue_brush);
            if (hr == S_OK) hr = d2d_render_target_pop_layer(rt);
            hr_extra_pop = d2d_render_target_pop_layer(rt);
            if (hr == S_OK) hr = d2d_render_target_end_draw(rt);
            d2d_render_target_read_pixel(rt, 32, 32, &px_masked_in);
            d2d_render_target_read_pixel(rt, 8, 8, &px_masked_out);

            d2d_render_target_begin_draw(rt);
            d2d_render_target_push_layer(rt, &lp, NULL);
            hr_unbalanced = d2d_render_target_end_draw(rt);
        }

        int ok = (hr == S_OK &&
                  px_masked_in.b > 0.95f &&
                  px_masked_out.b < 0.05f &&
                  hr_extra_pop == D2DERR_POP_CALL_DID_NOT_MATCH_PUSH &&
                  hr_unbalanced == D2DERR_PUSH_POP_UNBALANCED);
        parity_record_exact(
            "d2d1_007",
            "LayerPushPop_GeometricMaskAndUnbalancedError",
            "d2d_render_target_push_pop_layer",
            hr,
            0,
            0,
            NULL,
            0,
            ok ? 1 : 0);
        if (blue_brush) d2d_brush_destroy(blue_brush);
        if (rt) d2d_render_target_destroy(rt);
        if (mask_geom) d2d_path_geometry_destroy(mask_geom);
        if (factory) d2d_factory_destroy(factory);
    }

    /* 8. Antialiasing_PerPrimitiveVsAliasedSubpixelEdge */
    {
        D2D1_FACTORY *factory = NULL;
        D2D1_PATH_GEOMETRY *tri = NULL;
        D2D1_RENDER_TARGET *rt = NULL;
        D2D1_BRUSH *white_brush = NULL;
        D2D1_COLOR_F black = {0.0f, 0.0f, 0.0f, 1.0f};
        D2D1_COLOR_F white = {1.0f, 1.0f, 1.0f, 1.0f};
        uint32_t aa_partial_pixels = 0;
        uint32_t aliased_partial_pixels = 0;

        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, NULL, NULL, (void **)&factory);
        if (hr == S_OK) hr = d2d_path_geometry_create(factory, &tri);
        if (hr == S_OK) {
            D2D1_POINT_2F p0 = {0.25f, 0.25f};
            D2D1_POINT_2F p1 = {31.75f, 16.25f};
            D2D1_POINT_2F p2 = {0.25f, 31.75f};
            d2d_path_geometry_begin_figure(tri, p0, D2D1_FIGURE_BEGIN_FILLED);
            d2d_path_geometry_add_line(tri, p1);
            d2d_path_geometry_add_line(tri, p2);
            d2d_path_geometry_end_figure(tri, D2D1_FIGURE_END_CLOSED);
            d2d_path_geometry_close(tri);
        }
        if (hr == S_OK) hr = d2d_render_target_create(32, 32, &rt);
        if (hr == S_OK) hr = d2d_solid_color_brush_create(&white, 1.0f, &white_brush);
        if (hr == S_OK) {
            uint32_t x, y;
            d2d_render_target_begin_draw(rt);
            d2d_render_target_clear(rt, &black);
            d2d_render_target_set_antialias_mode(rt, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            d2d_render_target_fill_geometry(rt, tri, white_brush, NULL);
            d2d_render_target_end_draw(rt);
            for (y = 0; y < 32; ++y) {
                for (x = 0; x < 32; ++x) {
                    D2D1_COLOR_F c = {0.0f, 0.0f, 0.0f, 0.0f};
                    d2d_render_target_read_pixel(rt, x, y, &c);
                    if (c.r > 0.05f && c.r < 0.95f) {
                        aa_partial_pixels++;
                    }
                }
            }

            d2d_render_target_begin_draw(rt);
            d2d_render_target_clear(rt, &black);
            d2d_render_target_set_antialias_mode(rt, D2D1_ANTIALIAS_MODE_ALIASED);
            d2d_render_target_fill_geometry(rt, tri, white_brush, NULL);
            d2d_render_target_end_draw(rt);
            for (y = 0; y < 32; ++y) {
                for (x = 0; x < 32; ++x) {
                    D2D1_COLOR_F c = {0.0f, 0.0f, 0.0f, 0.0f};
                    d2d_render_target_read_pixel(rt, x, y, &c);
                    if (c.r > 0.05f && c.r < 0.95f) {
                        aliased_partial_pixels++;
                    }
                }
            }
        }

        int ok = (hr == S_OK && aa_partial_pixels >= 10 && aliased_partial_pixels == 0);
        parity_record_exact(
            "d2d1_008",
            "Antialiasing_PerPrimitiveVsAliasedSubpixelEdge",
            "d2d_render_target_set_antialias_mode",
            hr,
            0,
            0,
            NULL,
            0,
            ok ? 1 : 0);
        if (white_brush) d2d_brush_destroy(white_brush);
        if (rt) d2d_render_target_destroy(rt);
        if (tri) d2d_path_geometry_destroy(tri);
        if (factory) d2d_factory_destroy(factory);
    }

    /* 9. LinearGradientBrush_ExtendModesAndGamma */
    {
        D2D1_GRADIENT_STOP stops[2] = {
            {0.0f, {0.0f, 0.0f, 0.0f, 1.0f}},
            {1.0f, {1.0f, 1.0f, 1.0f, 1.0f}}
        };
        D2D1_GRADIENT_STOP_COLLECTION *sc_clamp_22 = NULL;
        D2D1_GRADIENT_STOP_COLLECTION *sc_mirror_10 = NULL;
        D2D1_GRADIENT_STOP_COLLECTION *sc_wrap_22 = NULL;
        D2D1_BRUSH *b_clamp = NULL, *b_mirror = NULL, *b_wrap = NULL;
        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props = {{0.0f, 0.0f}, {100.0f, 0.0f}};
        D2D1_COLOR_F c_mid_22 = {0}, c_mid_10 = {0}, c_clamp_over = {0}, c_mirror_over = {0}, c_wrap_over = {0};

        HRESULT hr = d2d_gradient_stop_collection_create(
            stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &sc_clamp_22);
        if (hr == S_OK) {
            hr = d2d_gradient_stop_collection_create(
                stops, 2, D2D1_GAMMA_1_0, D2D1_EXTEND_MODE_MIRROR, &sc_mirror_10);
        }
        if (hr == S_OK) {
            hr = d2d_gradient_stop_collection_create(
                stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_WRAP, &sc_wrap_22);
        }
        if (hr == S_OK) hr = d2d_linear_gradient_brush_create(&props, 1.0f, sc_clamp_22, &b_clamp);
        if (hr == S_OK) hr = d2d_linear_gradient_brush_create(&props, 1.0f, sc_mirror_10, &b_mirror);
        if (hr == S_OK) hr = d2d_linear_gradient_brush_create(&props, 1.0f, sc_wrap_22, &b_wrap);

        if (hr == S_OK) {
            D2D1_POINT_2F pt_mid = {50.0f, 0.0f};
            D2D1_POINT_2F pt_over = {125.0f, 0.0f};
            d2d_gradient_brush_sample(b_clamp, pt_mid, &c_mid_22);
            d2d_gradient_brush_sample(b_mirror, pt_mid, &c_mid_10);
            d2d_gradient_brush_sample(b_clamp, pt_over, &c_clamp_over);
            d2d_gradient_brush_sample(b_mirror, pt_over, &c_mirror_over);
            d2d_gradient_brush_sample(b_wrap, pt_over, &c_wrap_over);
        }

        int ok = (hr == S_OK &&
                  float_near(c_mid_22.r, 0.5f, 0.02f) &&
                  c_mid_10.r > 0.68f &&
                  float_near(c_clamp_over.r, 1.0f, 0.02f) &&
                  float_near(c_wrap_over.r, 0.25f, 0.02f) &&
                  c_mirror_over.r > 0.70f && c_mirror_over.r < 0.95f);
        parity_record_exact(
            "d2d1_009",
            "LinearGradientBrush_ExtendModesAndGamma",
            "d2d_gradient_brush_sample",
            hr,
            0,
            0,
            NULL,
            0,
            ok ? 1 : 0);
        if (b_clamp) d2d_brush_destroy(b_clamp);
        if (b_mirror) d2d_brush_destroy(b_mirror);
        if (b_wrap) d2d_brush_destroy(b_wrap);
        if (sc_clamp_22) d2d_gradient_stop_collection_destroy(sc_clamp_22);
        if (sc_mirror_10) d2d_gradient_stop_collection_destroy(sc_mirror_10);
        if (sc_wrap_22) d2d_gradient_stop_collection_destroy(sc_wrap_22);
    }

    /* 10. RadialGradientBrush_FocalOffsetAndFill */
    {
        D2D1_GRADIENT_STOP stops[2] = {
            {0.0f, {1.0f, 1.0f, 0.0f, 1.0f}},
            {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}}
        };
        D2D1_GRADIENT_STOP_COLLECTION *sc = NULL;
        D2D1_BRUSH *radial = NULL;
        D2D1_RENDER_TARGET *rt = NULL;
        D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES rprops = {
            {32.0f, 32.0f},
            {-10.0f, 0.0f},
            20.0f,
            20.0f
        };
        D2D1_COLOR_F c_focal = {0}, c_edge = {0}, px_rt = {0};

        HRESULT hr = d2d_gradient_stop_collection_create(
            stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &sc);
        if (hr == S_OK) hr = d2d_radial_gradient_brush_create(&rprops, 1.0f, sc, &radial);
        if (hr == S_OK) {
            D2D1_POINT_2F pt_focal = {22.0f, 32.0f};
            D2D1_POINT_2F pt_edge = {52.0f, 32.0f};
            d2d_gradient_brush_sample(radial, pt_focal, &c_focal);
            d2d_gradient_brush_sample(radial, pt_edge, &c_edge);
        }
        if (hr == S_OK) hr = d2d_render_target_create(64, 64, &rt);
        if (hr == S_OK) {
            D2D1_RECT_F rc = {0.0f, 0.0f, 64.0f, 64.0f};
            d2d_render_target_begin_draw(rt);
            d2d_render_target_fill_rectangle(rt, &rc, radial);
            d2d_render_target_end_draw(rt);
            d2d_render_target_read_pixel(rt, 22, 32, &px_rt);
        }

        int ok = (hr == S_OK &&
                  float_near(c_focal.r, 1.0f, 0.02f) &&
                  float_near(c_focal.b, 0.0f, 0.02f) &&
                  float_near(c_edge.r, 0.0f, 0.02f) &&
                  float_near(c_edge.b, 1.0f, 0.02f) &&
                  px_rt.r > 0.90f);
        parity_record_exact(
            "d2d1_010",
            "RadialGradientBrush_FocalOffsetAndFill",
            "d2d_gradient_brush_sample",
            hr,
            0,
            0,
            NULL,
            0,
            ok ? 1 : 0);
        if (rt) d2d_render_target_destroy(rt);
        if (radial) d2d_brush_destroy(radial);
        if (sc) d2d_gradient_stop_collection_destroy(sc);
    }

    /* 11. Vkd3dShaderMsl_PixelAndVertexTranslation */
    {
        VKD3D_SHADER_SOURCE_DESC desc;
        VKD3D_SHADER_MSL_OUTPUT out_msl;
        const char *ps_ir =
            "ps_5_0\n"
            "dcl_constant_buffer cb0[4], immediateIndexed\n"
            "dcl_resource_texture2d (float,float,float,float) t0\n"
            "dcl_sampler s0, mode_default\n"
            "dcl_input_ps linear v0.xy\n"
            "dcl_input_ps linear v1.xyz\n"
            "dcl_output o0.xyzw\n"
            "dcl_temps 2\n"
            "sample r0, v0.xyxx, t0.xyzw, s0\n"
            "dp3 r1.x, v1.xyzx, v1.xyzx\n"
            "rsq r1.x, r1.x\n"
            "mul r1.xyz, v1.xyzx, r1.xxxx\n"
            "mad o0, r0, cb0[0], r1.xyzx\n"
            "ret\n";

        memset(&desc, 0, sizeof(desc));
        memset(&out_msl, 0, sizeof(out_msl));
        desc.hlsl_ir_text = ps_ir;
        desc.entry_point = "ps_main";
        desc.shader_type = VKD3D_SHADER_TYPE_PIXEL;
        desc.flags = VKD3D_SHADER_COMPILE_FLAG_VERIFY_METAL_GPU;

        HRESULT hr = vkd3d_shader_compile_msl(&desc, &out_msl);
        int ok = (hr == S_OK &&
                  out_msl.msl_length > 100 &&
                  out_msl.cbuffer_count == 1 &&
                  out_msl.texture_count == 1 &&
                  out_msl.sampler_count == 1 &&
                  out_msl.input_count == 2 &&
                  out_msl.output_count == 1 &&
                  out_msl.instruction_count == 6 &&
                  strstr(out_msl.msl_source, "#include <metal_stdlib>") != NULL &&
                  strstr(out_msl.msl_source, "fragment ") != NULL &&
                  strstr(out_msl.msl_source, ".sample(") != NULL &&
                  strstr(out_msl.msl_source, "rsqrt(") != NULL &&
                  strstr(out_msl.msl_source, "fma(") != NULL &&
                  out_msl.metal_library_compiled == TRUE);
        parity_record_exact(
            "d2d1_011",
            "Vkd3dShaderMsl_PixelAndVertexTranslation",
            "vkd3d_shader_compile_msl",
            hr,
            0,
            0,
            NULL,
            0,
            ok ? 1 : 0);
    }

    /* 12. Vkd3dShaderMsl_ZeroInfSafeMulAndCompute */
    {
        VKD3D_SHADER_SOURCE_DESC desc;
        VKD3D_SHADER_MSL_OUTPUT out_msl;
        const char *cs_ir =
            "cs_5_0\n"
            "dcl_constant_buffer cb0[2], immediateIndexed\n"
            "dcl_input v0.xyzw\n"
            "dcl_output o0.xyzw\n"
            "dcl_temps 1\n"
            "mul r0, v0, cb0[0]\n"
            "add o0, r0, cb0[1]\n"
            "ret\n";

        memset(&desc, 0, sizeof(desc));
        memset(&out_msl, 0, sizeof(out_msl));
        desc.hlsl_ir_text = cs_ir;
        desc.entry_point = "cs_main";
        desc.shader_type = VKD3D_SHADER_TYPE_COMPUTE;
        desc.flags = VKD3D_SHADER_COMPILE_FLAG_ZERO_INF_MUL | VKD3D_SHADER_COMPILE_FLAG_VERIFY_METAL_GPU;

        HRESULT hr = vkd3d_shader_compile_msl(&desc, &out_msl);
        int ok = (hr == S_OK &&
                  strstr(out_msl.msl_source, "vkd3d_safe_mul(") != NULL &&
                  strstr(out_msl.msl_source, "kernel void cs_main") != NULL &&
                  out_msl.metal_library_compiled == TRUE);
        parity_record_exact(
            "d2d1_012",
            "Vkd3dShaderMsl_ZeroInfSafeMulAndCompute",
            "vkd3d_shader_compile_msl",
            hr,
            0,
            0,
            NULL,
            0,
            ok ? 1 : 0);
    }

    parity_end_document();
    return 0;
}
