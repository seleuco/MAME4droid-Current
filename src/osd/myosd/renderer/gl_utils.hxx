// license:BSD-3-Clause
// copyright-holders:Filipe Paulino (FlykeSpice)
/***************************************************************************

    gl_utils.hxx

    Common GLES utilities for MAME4droid

***************************************************************************/


#pragma once

#ifndef MAME4DROID_GLUTILS
#define MAME4DROID_GLUTILS

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <array>
#include <string>
#include <utility>
#include <vector>


namespace gl_utils
{
	inline std::array<float, 4*4> make_ortho(
			float left, float right,
			float bottom, float top,
			float nearZ, float farZ)
	{
		std::array<float, 4*4> m;

		float rl = right - left;
		float tb = top - bottom;
		float fn = farZ - nearZ;

		m[0]  =  2.0f / rl;
		m[1]  =  0.0f;
		m[2]  =  0.0f;
		m[3]  =  0.0f;

		m[4]  =  0.0f;
		m[5]  =  2.0f / tb;
		m[6]  =  0.0f;
		m[7]  =  0.0f;

		m[8]  =  0.0f;
		m[9]  =  0.0f;
		m[10] = -2.0f / fn;
		m[11] =  0.0f;

		m[12] = -(right + left) / rl;
		m[13] = -(top + bottom) / tb;
		m[14] = -(farZ + nearZ) / fn;
		m[15] =  1.0f;

		return m;
	}

	inline GLuint load_shader(const char* shader_src, GLenum type)
	{
		GLuint shader = glCreateShader(type);

		if (shader == 0)
			throw std::runtime_error("GLES2: unable to allocate a shader object");

		std::string _shaderSrc = "#version 100\n"; //GLES2 glsl version
		if (type == GL_FRAGMENT_SHADER)
		{
			_shaderSrc +=
				"#define FRAGMENT 1\n"
				"#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
				"	precision highp float;\n"
				"#else\n"
				"	precision mediump float;\n"
				"#endif\n";
		}
		else
			_shaderSrc += "#define VERTEX 1\n";

		_shaderSrc += shader_src;

		//Load the shader source
		const char* src = _shaderSrc.c_str();
		glShaderSource(shader, 1, &src, NULL);

		glCompileShader(shader);

		GLint compiled;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
		if (!compiled)
		{
			static char infoLog[300];

			glGetShaderInfoLog(shader, sizeof infoLog, NULL, infoLog);
			throw std::runtime_error(std::string("GLES2: Failure on compiling shaders:\n") + infoLog);
		}

		return shader;

	}

	inline GLuint create_program(GLuint vertex_shader, GLuint frag_shader, const std::vector<std::pair<GLuint, const char*>>& bindings = {})
	{
		//Now link them into a program object
		GLuint programObject = glCreateProgram();
		
		if (programObject == 0)
			throw std::runtime_error("GLES2: Unable to allocate a program object");

		//Bind the attrib locations
		for (auto [attrib, name] : bindings)
			glBindAttribLocation(programObject, attrib, name);

		glAttachShader(programObject, vertex_shader);
		glAttachShader(programObject, frag_shader);

		//Link the program object
		glLinkProgram(programObject);

		return programObject;
	}
}

#endif //MAME4DROID_GLUTILS
