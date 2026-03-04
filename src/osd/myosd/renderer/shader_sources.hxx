// license:BSD-3-Clause
// copyright-holders:Filipe Paulino (FlykeSpice)
/***************************************************************************

    shader_sources.hxx

    Shader sources for GLES2 renderer

***************************************************************************/

//=================================================
// Quad primitive program
// ================================================

static const char* const quad_vertex_shader_src = R"(
attribute vec2 a_position;
attribute vec2 a_texuv;

uniform mat4 u_ortho;

varying vec2 v_texuv;

void main()
{
	gl_Position = u_ortho * vec4(a_position, 0.0, 1.0);
	v_texuv = a_texuv;
}
)";

static const char* const quad_frag_shader_src = R"(
varying vec2 v_texuv;
uniform sampler2D s_texture;
uniform vec4 u_color;

void main()
{
	gl_FragColor = texture2D(s_texture, v_texuv) * u_color;
}
)";

//================================================
// Line primitive program
//================================================

static const char* const line_vertex_shader_src = R"(
attribute vec2 a_position;

uniform mat4 u_ortho;

void main()
{
	gl_Position = u_ortho * vec4(a_position, 0.0, 1.0); 
}
)";

static const char* const line_frag_shader_src = R"(
uniform vec4 u_color;

void main()
{
	gl_FragColor = u_color;
}
)";
