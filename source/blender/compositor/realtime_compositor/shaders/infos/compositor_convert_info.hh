/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "gpu_shader_create_info.hh"

GPU_SHADER_CREATE_INFO(compositor_convert_shared)
    .local_group_size(16, 16)
    .sampler(0, ImageType::FLOAT_2D, "input_tx")
    .typedef_source("gpu_shader_compositor_type_conversion.glsl")
    .compute_source("compositor_convert.glsl");

GPU_SHADER_CREATE_INFO(compositor_convert_float_to_float)
    .additional_info("compositor_convert_shared")
    .image(0, GPU_R16F, Qualifier::WRITE, ImageType::FLOAT_2D, "output_img")
    .define("CONVERT_EXPRESSION(value)", "value")
    .do_static_compilation(true);

GPU_SHADER_CREATE_INFO(compositor_convert_float_to_vector)
    .additional_info("compositor_convert_shared")
    .image(0, GPU_RGBA16F, Qualifier::WRITE, ImageType::FLOAT_2D, "output_img")
    .define("CONVERT_EXPRESSION(value)", "vec4(vec3_from_float(value.x), 1.0)")
    .do_static_compilation(true);

GPU_SHADER_CREATE_INFO(compositor_convert_float_to_color)
    .additional_info("compositor_convert_shared")
    .image(0, GPU_RGBA16F, Qualifier::WRITE, ImageType::FLOAT_2D, "output_img")
    .define("CONVERT_EXPRESSION(value)", "vec4_from_float(value.x)")
    .do_static_compilation(true);

GPU_SHADER_CREATE_INFO(compositor_convert_color_to_float)
    .additional_info("compositor_convert_shared")
    .image(0, GPU_R16F, Qualifier::WRITE, ImageType::FLOAT_2D, "output_img")
    .define("CONVERT_EXPRESSION(value)", "vec4(float_from_vec4(value), vec3(0.0))")
    .do_static_compilation(true);

GPU_SHADER_CREATE_INFO(compositor_convert_color_to_vector)
    .additional_info("compositor_convert_shared")
    .image(0, GPU_RGBA16F, Qualifier::WRITE, ImageType::FLOAT_2D, "output_img")
    .define("CONVERT_EXPRESSION(value)", "value")
    .do_static_compilation(true);

GPU_SHADER_CREATE_INFO(compositor_convert_color_to_color)
    .additional_info("compositor_convert_shared")
    .image(0, GPU_RGBA16F, Qualifier::WRITE, ImageType::FLOAT_2D, "output_img")
    .define("CONVERT_EXPRESSION(value)", "value")
    .do_static_compilation(true);

GPU_SHADER_CREATE_INFO(compositor_convert_vector_to_float)
    .additional_info("compositor_convert_shared")
    .image(0, GPU_R16F, Qualifier::WRITE, ImageType::FLOAT_2D, "output_img")
    .define("CONVERT_EXPRESSION(value)", "vec4(float_from_vec3(value.xyz), vec3(0.0))")
    .do_static_compilation(true);

GPU_SHADER_CREATE_INFO(compositor_convert_vector_to_vector)
    .additional_info("compositor_convert_shared")
    .image(0, GPU_RGBA16F, Qualifier::WRITE, ImageType::FLOAT_2D, "output_img")
    .define("CONVERT_EXPRESSION(value)", "value")
    .do_static_compilation(true);

GPU_SHADER_CREATE_INFO(compositor_convert_vector_to_color)
    .additional_info("compositor_convert_shared")
    .image(0, GPU_RGBA16F, Qualifier::WRITE, ImageType::FLOAT_2D, "output_img")
    .define("CONVERT_EXPRESSION(value)", "vec4_from_vec3(value.xyz)")
    .do_static_compilation(true);

GPU_SHADER_CREATE_INFO(compositor_convert_color_to_alpha)
    .additional_info("compositor_convert_shared")
    .image(0, GPU_R16F, Qualifier::WRITE, ImageType::FLOAT_2D, "output_img")
    .define("CONVERT_EXPRESSION(value)", "vec4(value.a)")
    .do_static_compilation(true);
