// license:BSD-3-Clause
// copyright-holders:David Valdeita (Seleuco) & Filipe Paulino (FlykeSpice)
/***************************************************************************

    shader_sources.hxx

    Shader sources for GLES2 renderer

***************************************************************************/

//=================================================
// Quad primitive program
// ================================================

static const char* quad_vertex_shader_src = R"(
    attribute vec4 a_position;
    attribute vec2 a_texuv;
    attribute vec4 a_color;      
    varying vec2 v_texuv;
    varying vec4 v_color;        
    uniform mat4 u_ortho;
    void main() {
        gl_Position = u_ortho * a_position;
        v_texuv = a_texuv;
        v_color = a_color;       
    }
)";

static const char* quad_frag_shader_src = R"(
    precision mediump float;
    varying vec2 v_texuv;
    varying vec4 v_color;
    uniform sampler2D s_texture;
    void main() {
        gl_FragColor = texture2D(s_texture, v_texuv) * v_color;
    }
)";

