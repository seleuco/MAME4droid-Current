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
#include <string>


namespace gl_utils
{
	inline GLuint load_shader(const char* shader_src, GLenum type)
	{
		GLuint shader = glCreateShader(type);

		if (shader == 0)
			throw std::runtime_error("GLES2: unable to allocate a shader object");

		std::string _shaderSrc = "#version 100\n"; //GLES2 glsl version
		if (type == GL_FRAGMENT_SHADER)
		{
			_shaderSrc +=
				"#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
				"	precision highp float;\n"
				"#else\n"
				"	precision mediump float;\n"
				"#endif\n";
		}
		_shaderSrc += shader_src;

		//Load the shader source
		const char* src = _shaderSrc.c_str();
		glShaderSource(shader, 1, &src, NULL);

		glCompileShader(shader);

		GLint compiled;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
		if (!compiled)
		{
			static char infoLog[100];

			glGetShaderInfoLog(shader, 100, NULL, infoLog);
			throw std::runtime_error(std::string("GLES2: Failure on compiling shaders: ") + infoLog);
		}

		return shader;

	}

	inline GLuint create_program(GLuint vertex_shader, GLuint frag_shader)
	{
		//Now link them into a program object
		GLuint programObject = glCreateProgram();
		
		if (programObject == 0)
			throw std::runtime_error("GLES2: Unable to allocate a program object");

		glAttachShader(programObject, vertex_shader);
		glAttachShader(programObject, frag_shader);

		//Link the program object
		glLinkProgram(programObject);

		return programObject;
	}
}

#endif //MAME4DROID_GLUTILS
