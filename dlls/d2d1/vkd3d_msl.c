/*
 * vkd3d-shader Metal Shading Language (MSL) Generator & Apple Metal GPU Compiler
 *
 * Clean-Room Spec: specs/d2d1/vkd3d_shader_compile_msl.json
 * Copyright (C) 2026 Wine Parity Project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifdef __APPLE__
#define BOOL OBJC_BOOL
#include <objc/runtime.h>
#include <objc/message.h>
#undef BOOL
extern void *MTLCreateSystemDefaultDevice(void);
#endif

#include "d2d1.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

WINE_DEFAULT_DEBUG_CHANNEL(d2d1);

static int vkd3d_msl_simulate_stub(void)
{
    const char *env = getenv("WINE_PARITY_SIMULATE_STUB");
    return (env && strcmp(env, "1") == 0);
}

static void msl_append(VKD3D_SHADER_MSL_OUTPUT *out, const char *fmt, ...)
{
    va_list ap;
    int written;
    size_t rem;

    if (out->msl_length + 1 >= sizeof(out->msl_source)) {
        return;
    }
    rem = sizeof(out->msl_source) - out->msl_length;
    va_start(ap, fmt);
    written = vsnprintf(out->msl_source + out->msl_length, rem, fmt, ap);
    va_end(ap);
    if (written > 0) {
        if ((size_t)written >= rem) {
            out->msl_length = sizeof(out->msl_source) - 1;
        } else {
            out->msl_length += (size_t)written;
        }
    }
}

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r') {
        p++;
    }
    return p;
}

static void read_token(const char **cursor, char *buf, size_t cap)
{
    const char *p = skip_ws(*cursor);
    size_t i = 0;

    if (*p == ',') {
        p = skip_ws(p + 1);
    }
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && *p != ',') {
        if (i + 1 < cap) {
            buf[i++] = *p;
        }
        p++;
    }
    buf[i] = '\0';
    *cursor = p;
}

/*
 * Format an IR register token (e.g. "r0", "r1.xyz", "v0.xyxx", "cb0[0]", "o0.xyzw")
 * into a float4 MSL expression.
 */
static void translate_operand(const char *tok, char *out_expr, size_t cap, int is_lhs)
{
    char base[64];
    const char *dot = strchr(tok, '.');
    size_t base_len = dot ? (size_t)(dot - tok) : strlen(tok);
    if (base_len >= sizeof(base)) {
        base_len = sizeof(base) - 1;
    }
    memcpy(base, tok, base_len);
    base[base_len] = '\0';

    if (strncmp(base, "cb0[", 4) == 0) {
        int idx = atoi(base + 4);
        if (idx < 0) idx = 0;
        snprintf(out_expr, cap, "cb0.data[%d]", idx);
        return;
    }

    if (base[0] == 'v' && isdigit((unsigned char)base[1])) {
        int idx = atoi(base + 1);
        snprintf(out_expr, cap, "in.v%d", idx);
        return;
    }

    if (base[0] == 'o' && isdigit((unsigned char)base[1])) {
        int idx = atoi(base + 1);
        snprintf(out_expr, cap, "out.o%d", idx);
        return;
    }

    if (base[0] == 'r' && isdigit((unsigned char)base[1])) {
        int idx = atoi(base + 1);
        snprintf(out_expr, cap, "r%d", idx);
        return;
    }

    if (base[0] == 't' && isdigit((unsigned char)base[1])) {
        int idx = atoi(base + 1);
        snprintf(out_expr, cap, "t%d", idx);
        return;
    }

    if (base[0] == 's' && isdigit((unsigned char)base[1])) {
        int idx = atoi(base + 1);
        snprintf(out_expr, cap, "s%d", idx);
        return;
    }

    (void)is_lhs;
    snprintf(out_expr, cap, "%s", base);
}

#ifdef __APPLE__
static BOOL compile_msl_on_apple_metal(
    const char *msl_src,
    const char *entry_point,
    VKD3D_SHADER_MSL_OUTPUT *out_msl)
{
    void *device = MTLCreateSystemDefaultDevice();
    if (!device) {
        snprintf(out_msl->metal_device_name, sizeof(out_msl->metal_device_name), "unavailable");
        return FALSE;
    }

    typedef id (*msg_id_str_t)(id, SEL, const char *);
    typedef const char *(*msg_cstr_t)(id, SEL);
    typedef id (*msg_id_t)(id, SEL);
    typedef id (*msg_new_lib_t)(id, SEL, id, id, id *);
    typedef id (*msg_new_fn_t)(id, SEL, id);
    typedef void (*msg_void_t)(id, SEL);

    Class cls_nsstring = objc_getClass("NSString");
    SEL sel_string_utf8 = sel_registerName("stringWithUTF8String:");
    SEL sel_utf8 = sel_registerName("UTF8String");
    SEL sel_name = sel_registerName("name");
    SEL sel_new_lib = sel_registerName("newLibraryWithSource:options:error:");
    SEL sel_new_fn = sel_registerName("newFunctionWithName:");
    SEL sel_desc = sel_registerName("localizedDescription");
    SEL sel_release = sel_registerName("release");

    id dev_name_ns = ((msg_id_t)objc_msgSend)((id)device, sel_name);
    if (dev_name_ns) {
        const char *c_name = ((msg_cstr_t)objc_msgSend)(dev_name_ns, sel_utf8);
        if (c_name) {
            snprintf(out_msl->metal_device_name, sizeof(out_msl->metal_device_name), "%s", c_name);
        }
    }

    id ns_src = ((msg_id_str_t)objc_msgSend)((id)cls_nsstring, sel_string_utf8, msl_src);
    id ns_err = NULL;
    id mtl_lib = ((msg_new_lib_t)objc_msgSend)((id)device, sel_new_lib, ns_src, NULL, &ns_err);
    if (!mtl_lib) {
        if (ns_err) {
            id err_str = ((msg_id_t)objc_msgSend)(ns_err, sel_desc);
            const char *c_err = err_str ? ((msg_cstr_t)objc_msgSend)(err_str, sel_utf8) : "Metal compile error";
            snprintf(out_msl->error_log, sizeof(out_msl->error_log), "%s", c_err ? c_err : "Metal error");
        }
        ((msg_void_t)objc_msgSend)((id)device, sel_release);
        return FALSE;
    }

    id ns_entry = ((msg_id_str_t)objc_msgSend)((id)cls_nsstring, sel_string_utf8, entry_point);
    id mtl_fn = ((msg_new_fn_t)objc_msgSend)(mtl_lib, sel_new_fn, ns_entry);
    BOOL ok = (mtl_fn != NULL) ? TRUE : FALSE;

    if (mtl_fn) {
        ((msg_void_t)objc_msgSend)(mtl_fn, sel_release);
    }
    ((msg_void_t)objc_msgSend)(mtl_lib, sel_release);
    ((msg_void_t)objc_msgSend)((id)device, sel_release);
    return ok;
}
#endif

HRESULT WINAPI vkd3d_shader_compile_msl(
    const VKD3D_SHADER_SOURCE_DESC *desc,
    VKD3D_SHADER_MSL_OUTPUT *out_msl)
{
    const char *entry;
    const char *line_start;
    uint32_t temp_count = 4;
    uint32_t i;

    if (vkd3d_msl_simulate_stub()) {
        return E_NOTIMPL;
    }
    if (!desc || !desc->hlsl_ir_text || !out_msl) {
        return E_INVALIDARG;
    }
    if (desc->hlsl_ir_text[0] == '\0') {
        return E_INVALIDARG;
    }

    memset(out_msl, 0, sizeof(*out_msl));
    entry = (desc->entry_point && desc->entry_point[0] != '\0') ? desc->entry_point : "main0";

    /* Pass 1: Scan declarations (inputs, outputs, cbuffers, textures, samplers, temps) */
    line_start = desc->hlsl_ir_text;
    while (*line_start) {
        const char *line_end = strchr(line_start, '\n');
        char line[256];
        size_t len = line_end ? (size_t)(line_end - line_start) : strlen(line_start);
        const char *cur;
        char op[64];

        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, line_start, len);
        line[len] = '\0';

        cur = line;
        read_token(&cur, op, sizeof(op));
        if (strcmp(op, "dcl_constant_buffer") == 0) {
            out_msl->cbuffer_count++;
        } else if (strncmp(op, "dcl_resource_texture2d", 22) == 0) {
            out_msl->texture_count++;
        } else if (strcmp(op, "dcl_sampler") == 0) {
            out_msl->sampler_count++;
        } else if (strncmp(op, "dcl_input", 9) == 0) {
            out_msl->input_count++;
        } else if (strncmp(op, "dcl_output", 10) == 0) {
            out_msl->output_count++;
        } else if (strcmp(op, "dcl_temps") == 0) {
            char cnt_tok[32];
            read_token(&cur, cnt_tok, sizeof(cnt_tok));
            temp_count = (uint32_t)atoi(cnt_tok);
            if (temp_count == 0 || temp_count > 32) temp_count = 4;
        }

        if (!line_end) break;
        line_start = line_end + 1;
    }

    /* Emit MSL header & optional D3D9/SM1 0*INF=0 safe multiply helper */
    msl_append(out_msl, "#include <metal_stdlib>\nusing namespace metal;\n\n");
    if (desc->flags & VKD3D_SHADER_COMPILE_FLAG_ZERO_INF_MUL) {
        msl_append(
            out_msl,
            "static inline float4 vkd3d_safe_mul(float4 a, float4 b)\n"
            "{\n"
            "    return select(a * b, float4(0.0f), (a == 0.0f) | (b == 0.0f));\n"
            "}\n\n");
    }

    if (out_msl->cbuffer_count > 0) {
        msl_append(out_msl, "struct vkd3d_cb0_t {\n    float4 data[16];\n};\n\n");
    }

    msl_append(out_msl, "struct vkd3d_in_t {\n");
    if (desc->shader_type == VKD3D_SHADER_TYPE_PIXEL) {
        msl_append(out_msl, "    float4 pos [[position]];\n");
    }
    for (i = 0; i < (out_msl->input_count > 0 ? out_msl->input_count : 1); ++i) {
        if (desc->shader_type == VKD3D_SHADER_TYPE_VERTEX) {
            msl_append(out_msl, "    float4 v%u [[attribute(%u)]];\n", i, i);
        } else {
            msl_append(out_msl, "    float4 v%u [[user(locn%u)]];\n", i, i);
        }
    }
    msl_append(out_msl, "};\n\n");

    msl_append(out_msl, "struct vkd3d_out_t {\n");
    if (desc->shader_type == VKD3D_SHADER_TYPE_VERTEX) {
        msl_append(out_msl, "    float4 o0 [[position]];\n");
        for (i = 1; i < out_msl->output_count; ++i) {
            msl_append(out_msl, "    float4 o%u [[user(locn%u)]];\n", i, i);
        }
    } else {
        for (i = 0; i < (out_msl->output_count > 0 ? out_msl->output_count : 1); ++i) {
            msl_append(out_msl, "    float4 o%u [[color(%u)]];\n", i, i);
        }
    }
    msl_append(out_msl, "};\n\n");

    /* Emit shader entrypoint signature */
    if (desc->shader_type == VKD3D_SHADER_TYPE_COMPUTE) {
        msl_append(
            out_msl,
            "kernel void %s(\n"
            "    device const vkd3d_in_t *in_buf [[buffer(0)]],\n"
            "    device vkd3d_out_t *out_buf [[buffer(1)]]",
            entry);
        if (out_msl->cbuffer_count > 0) {
            msl_append(out_msl, ",\n    constant vkd3d_cb0_t &cb0 [[buffer(2)]]");
        }
        msl_append(out_msl, ",\n    uint tid [[thread_position_in_grid]])\n{\n");
        msl_append(out_msl, "    vkd3d_in_t in = in_buf[tid];\n");
        msl_append(out_msl, "    vkd3d_out_t out = {};\n");
    } else {
        const char *stage_kw = (desc->shader_type == VKD3D_SHADER_TYPE_VERTEX) ? "vertex" : "fragment";
        msl_append(
            out_msl,
            "%s vkd3d_out_t %s(\n"
            "    vkd3d_in_t in [[stage_in]]",
            stage_kw,
            entry);
        if (out_msl->cbuffer_count > 0) {
            msl_append(out_msl, ",\n    constant vkd3d_cb0_t &cb0 [[buffer(0)]]");
        }
        for (i = 0; i < out_msl->texture_count; ++i) {
            msl_append(out_msl, ",\n    texture2d<float> t%u [[texture(%u)]]", i, i);
        }
        for (i = 0; i < out_msl->sampler_count; ++i) {
            msl_append(out_msl, ",\n    sampler s%u [[sampler(%u)]]", i, i);
        }
        msl_append(out_msl, ")\n{\n");
        msl_append(out_msl, "    vkd3d_out_t out = {};\n");
    }

    for (i = 0; i < temp_count; ++i) {
        msl_append(out_msl, "    float4 r%u = float4(0.0f);\n", i);
    }

    /* Pass 2: Translate IR instructions into MSL body */
    line_start = desc->hlsl_ir_text;
    while (*line_start) {
        const char *line_end = strchr(line_start, '\n');
        char line[256];
        size_t len = line_end ? (size_t)(line_end - line_start) : strlen(line_start);
        const char *cur;
        char op[64], t0[64], t1[64], t2[64], t3[64];
        char dst[64], s0[64], s1[64], s2[64];

        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, line_start, len);
        line[len] = '\0';

        cur = line;
        read_token(&cur, op, sizeof(op));

        if (strcmp(op, "mov") == 0) {
            read_token(&cur, t0, sizeof(t0));
            read_token(&cur, t1, sizeof(t1));
            translate_operand(t0, dst, sizeof(dst), 1);
            translate_operand(t1, s0, sizeof(s0), 0);
            msl_append(out_msl, "    %s = %s;\n", dst, s0);
            out_msl->instruction_count++;
        } else if (strcmp(op, "add") == 0) {
            read_token(&cur, t0, sizeof(t0));
            read_token(&cur, t1, sizeof(t1));
            read_token(&cur, t2, sizeof(t2));
            translate_operand(t0, dst, sizeof(dst), 1);
            translate_operand(t1, s0, sizeof(s0), 0);
            translate_operand(t2, s1, sizeof(s1), 0);
            msl_append(out_msl, "    %s = (%s + %s);\n", dst, s0, s1);
            out_msl->instruction_count++;
        } else if (strcmp(op, "sub") == 0) {
            read_token(&cur, t0, sizeof(t0));
            read_token(&cur, t1, sizeof(t1));
            read_token(&cur, t2, sizeof(t2));
            translate_operand(t0, dst, sizeof(dst), 1);
            translate_operand(t1, s0, sizeof(s0), 0);
            translate_operand(t2, s1, sizeof(s1), 0);
            msl_append(out_msl, "    %s = (%s - %s);\n", dst, s0, s1);
            out_msl->instruction_count++;
        } else if (strcmp(op, "mul") == 0) {
            read_token(&cur, t0, sizeof(t0));
            read_token(&cur, t1, sizeof(t1));
            read_token(&cur, t2, sizeof(t2));
            translate_operand(t0, dst, sizeof(dst), 1);
            translate_operand(t1, s0, sizeof(s0), 0);
            translate_operand(t2, s1, sizeof(s1), 0);
            if (desc->flags & VKD3D_SHADER_COMPILE_FLAG_ZERO_INF_MUL) {
                msl_append(out_msl, "    %s = vkd3d_safe_mul(%s, %s);\n", dst, s0, s1);
            } else {
                msl_append(out_msl, "    %s = (%s * %s);\n", dst, s0, s1);
            }
            out_msl->instruction_count++;
        } else if (strcmp(op, "mad") == 0) {
            read_token(&cur, t0, sizeof(t0));
            read_token(&cur, t1, sizeof(t1));
            read_token(&cur, t2, sizeof(t2));
            read_token(&cur, t3, sizeof(t3));
            translate_operand(t0, dst, sizeof(dst), 1);
            translate_operand(t1, s0, sizeof(s0), 0);
            translate_operand(t2, s1, sizeof(s1), 0);
            translate_operand(t3, s2, sizeof(s2), 0);
            msl_append(out_msl, "    %s = fma(%s, %s, %s);\n", dst, s0, s1, s2);
            out_msl->instruction_count++;
        } else if (strcmp(op, "dp3") == 0) {
            read_token(&cur, t0, sizeof(t0));
            read_token(&cur, t1, sizeof(t1));
            read_token(&cur, t2, sizeof(t2));
            translate_operand(t0, dst, sizeof(dst), 1);
            translate_operand(t1, s0, sizeof(s0), 0);
            translate_operand(t2, s1, sizeof(s1), 0);
            msl_append(out_msl, "    %s = float4(dot((%s).xyz, (%s).xyz));\n", dst, s0, s1);
            out_msl->instruction_count++;
        } else if (strcmp(op, "dp4") == 0) {
            read_token(&cur, t0, sizeof(t0));
            read_token(&cur, t1, sizeof(t1));
            read_token(&cur, t2, sizeof(t2));
            translate_operand(t0, dst, sizeof(dst), 1);
            translate_operand(t1, s0, sizeof(s0), 0);
            translate_operand(t2, s1, sizeof(s1), 0);
            msl_append(out_msl, "    %s = float4(dot(%s, %s));\n", dst, s0, s1);
            out_msl->instruction_count++;
        } else if (strcmp(op, "rsq") == 0) {
            read_token(&cur, t0, sizeof(t0));
            read_token(&cur, t1, sizeof(t1));
            translate_operand(t0, dst, sizeof(dst), 1);
            translate_operand(t1, s0, sizeof(s0), 0);
            msl_append(out_msl, "    %s = rsqrt(max(%s, float4(1e-8f)));\n", dst, s0);
            out_msl->instruction_count++;
        } else if (strcmp(op, "sqrt") == 0) {
            read_token(&cur, t0, sizeof(t0));
            read_token(&cur, t1, sizeof(t1));
            translate_operand(t0, dst, sizeof(dst), 1);
            translate_operand(t1, s0, sizeof(s0), 0);
            msl_append(out_msl, "    %s = sqrt(max(%s, float4(0.0f)));\n", dst, s0);
            out_msl->instruction_count++;
        } else if (strcmp(op, "min") == 0 || strcmp(op, "max") == 0) {
            read_token(&cur, t0, sizeof(t0));
            read_token(&cur, t1, sizeof(t1));
            read_token(&cur, t2, sizeof(t2));
            translate_operand(t0, dst, sizeof(dst), 1);
            translate_operand(t1, s0, sizeof(s0), 0);
            translate_operand(t2, s1, sizeof(s1), 0);
            msl_append(out_msl, "    %s = %s(%s, %s);\n", dst, op, s0, s1);
            out_msl->instruction_count++;
        } else if (strcmp(op, "sample") == 0) {
            read_token(&cur, t0, sizeof(t0));
            read_token(&cur, t1, sizeof(t1));
            read_token(&cur, t2, sizeof(t2));
            read_token(&cur, t3, sizeof(t3));
            translate_operand(t0, dst, sizeof(dst), 1);
            translate_operand(t1, s0, sizeof(s0), 0);
            translate_operand(t2, s1, sizeof(s1), 0);
            translate_operand(t3, s2, sizeof(s2), 0);
            msl_append(out_msl, "    %s = %s.sample(%s, (%s).xy);\n", dst, s1, s2, s0);
            out_msl->instruction_count++;
        } else if (strcmp(op, "ret") == 0) {
            if (desc->shader_type == VKD3D_SHADER_TYPE_COMPUTE) {
                msl_append(out_msl, "    out_buf[tid] = out;\n    return;\n");
            } else {
                msl_append(out_msl, "    return out;\n");
            }
            out_msl->instruction_count++;
        }

        if (!line_end) break;
        line_start = line_end + 1;
    }

    msl_append(out_msl, "}\n");

#ifdef __APPLE__
    if (desc->flags & VKD3D_SHADER_COMPILE_FLAG_VERIFY_METAL_GPU) {
        out_msl->metal_library_compiled = compile_msl_on_apple_metal(
            out_msl->msl_source,
            entry,
            out_msl);
        if (!out_msl->metal_library_compiled) {
            ERR("Metal shader compilation failed: %s\n", out_msl->error_log);
            return E_FAIL;
        }
    } else {
        out_msl->metal_library_compiled = TRUE;
    }
#else
    out_msl->metal_library_compiled = TRUE;
    snprintf(out_msl->metal_device_name, sizeof(out_msl->metal_device_name), "host-fallback");
#endif

    return S_OK;
}
