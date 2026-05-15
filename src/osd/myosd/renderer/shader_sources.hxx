// license:BSD-3-Clause
// copyright-holders:David Valdeita (Seleuco) & Filipe Paulino (FlykeSpice)
/***************************************************************************

    shader_sources.hxx

    Shader sources for GLES2 renderer

***************************************************************************/


//=================================================
// Quad primitive program
// ================================================

//=================================================
// Quad primitive program
// ================================================

static const char* quad_vertex_shader_src = 
    "in float a_corner;\n"
    "in vec4 i_p0p1;\n"
    "in vec4 i_p2p3;\n"
    "in vec4 i_uv0uv1;\n"
    "in vec4 i_uv2uv3;\n"
    "in vec4 i_color;\n"
    "out vec2 v_texuv;\n"
    "out vec4 v_color;\n"
    "uniform mat4 u_ortho;\n"
    "void main() {\n"
    "    vec2 pos;\n"
    "    vec2 uv;\n"
    "    int corner = int(a_corner);\n"
    "    if (corner == 0)      { pos = i_p0p1.xy; uv = i_uv0uv1.xy; }\n"
    "    else if (corner == 1) { pos = i_p0p1.zw; uv = i_uv0uv1.zw; }\n"
    "    else if (corner == 2) { pos = i_p2p3.xy; uv = i_uv2uv3.xy; }\n"
    "    else                  { pos = i_p2p3.zw; uv = i_uv2uv3.zw; }\n"
    "    gl_Position = u_ortho * vec4(pos, 0.0, 1.0);\n"
    "    v_texuv = uv;\n"
    "    v_color = i_color;\n"
    "}\n";

static const char* quad_frag_shader_src = 
    "in vec2 v_texuv;\n"
    "in vec4 v_color;\n"
    "uniform sampler2D s_texture;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    fragColor = texture(s_texture, v_texuv) * v_color;\n"
    "}\n";
	
	
//=================================================
// HDR Tone Mapping Program
// ================================================

static const char* hdr_frag_shader_src = 
    "precision highp float;\n"
    "in vec2 v_texuv;\n"
    "in vec4 v_color;\n"
    "uniform sampler2D s_texture;\n"
    "uniform float u_exposure;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    vec4 hdrColor = texture(s_texture, v_texuv);\n"
    "    // Exponential Tone Mapping (Soft mapping from HDR to LDR)\n"
    "    vec3 mapped = vec3(1.0) - exp(-hdrColor.rgb * u_exposure);\n"
    "    fragColor = vec4(mapped, hdrColor.a) * v_color;\n"
    "}\n";
	