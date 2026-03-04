// license:BSD-3-Clause
// copyright-holders:Filipe Paulino (FlykeSpice)
/***************************************************************************

    filter_shader.cpp

    Effect shaders for MAME4droid's GLES2 renderer

***************************************************************************/


#include "filter_shader.h"
#include "gl_utils.hxx"
#include "gles2_renderer.h"

#include <android/log.h>

#include <cstdio>
#include <cstring> //std::strerror
#include <cerrno>
#include <stdexcept>

#define ANDROID_LOG(...) __android_log_print(ANDROID_LOG_DEBUG, "filter_shader", __VA_ARGS__)

void filter_shader::load_filter(const std::string& filter_src)
{
	//TODO: Cleanup previous filter state
	glDeleteProgram(m_program);

	auto vert_shader = gl_utils::load_shader(filter_src.c_str(), GL_VERTEX_SHADER);
	auto frag_shader = gl_utils::load_shader(filter_src.c_str(), GL_FRAGMENT_SHADER);

	m_program = gl_utils::create_program(vert_shader, frag_shader, {{gles2_renderer::ATTRIB_POSITION, "VertexCoord"}, {gles2_renderer::ATTRIB_TEXUV, "TexCoord"}});

	//Flag the shader objects to deletion once the associated program is also deleted
	glDeleteShader(vert_shader);
	glDeleteShader(frag_shader);

	//Effect shader additional uniforms...
	m_uniform_InputSize   = glGetUniformLocation(m_program, "InputSize");
	m_uniform_OutputSize  = glGetUniformLocation(m_program, "OutputSize");
	m_uniform_TextureSize = glGetUniformLocation(m_program, "TextureSize");
	m_uniform_FrameCount  = glGetUniformLocation(m_program, "FrameCount");
	m_framecount = 0;

	m_uniform_MVPMatrix = glGetUniformLocation(m_program, "MVPMatrix");

	//Setup some uniforms initial values
	glUseProgram(m_program);

	auto texture_sampler = glGetUniformLocation(m_program, "Texture");
	glUniform1i(texture_sampler, 0); // Screen texture will be always located at unit 0
	
	glUniform2f(m_uniform_TextureSize, m_texwidth, m_texheight);
	glUniform2f(m_uniform_InputSize,   m_texwidth, m_texheight);

	glUniformMatrix4fv(m_uniform_MVPMatrix, 1, GL_FALSE, m_ortho.data());
}

void filter_shader::set_input_size(int width, int height)
{
	m_texwidth = width; m_texheight = height;

	if (!m_program)
		return;

	GLint last_program;
	glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
	glUseProgram(m_program);

	glUniform2f(m_uniform_TextureSize, width, height);
	glUniform2f(m_uniform_InputSize,   width, height);

	
	glUseProgram(last_program);
}

void filter_shader::set_ortho(std::array<float, 4*4> ortho)
{
	m_ortho = ortho;

	if (!m_program)
		return;

	GLint last_program;
	glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
	glUseProgram(m_program);

	glUniformMatrix4fv(m_uniform_MVPMatrix, 1, GL_FALSE, ortho.data());

	glUseProgram(last_program);
}

void filter_shader::draw(int width, int height)
{
	glUseProgram(m_program);

	if (m_uniform_FrameCount != -1)
	{
		glUniform1i(m_uniform_FrameCount, ++m_framecount);
	}

	glUniform2f(m_uniform_OutputSize, width, height);

	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, gles2_renderer::s_quad_indices);
}

std::map<std::string, std::string> filter_shader::load_filters(const std::string &root_path)
{
	using namespace std::string_literals;

	std::map<std::string, std::string> filters;
	std::string path;
	std::string src_buf;

	path = root_path + "shaders.cfg";

	ANDROID_LOG("Opening %s...", path.c_str());
	std::FILE* file = std::fopen(path.c_str(), "r");

	if (!file)
		throw std::runtime_error("Failure on opening shaders.cfg to read shader filters: "s + std::strerror(errno));

	ANDROID_LOG("Opened, now reading lines entry...");

	char buf[150];
	while (std::fgets(buf, sizeof buf, file))
	{
		ANDROID_LOG("line: %s", buf);

		if (std::strlen(buf) <= 10 || buf[0] == '#') //Comment, skip
			continue;

		char shader_filename[100], name[100], linear[10];
		int version;

		int ret = std::sscanf(buf, "%[^;];%[^;];%[^;];%d", shader_filename, name, linear, &version);
		if (ret < 4)
		{
			std::fclose(file);
			throw std::runtime_error("Error extracting line fields with scanf, expected 4 but only matched "s + std::to_string(ret));
		}

		if (version != 1)
			continue;

		path = root_path + "shaders/" + shader_filename;
		std::FILE* shader_file = std::fopen(path.c_str(), "r");
		if (!shader_file)
		{
			std::fclose(file);
			throw std::runtime_error("error opening shader file "s + shader_filename);
		}

		std::fseek(shader_file, 0, SEEK_END);
		auto filelen = std::ftell(shader_file);
		std::rewind(shader_file);

		if (src_buf.size() < filelen)
			src_buf.resize(filelen);

		if (std::fread(src_buf.data(), 1, filelen, shader_file) != filelen)
		{
			std::fclose(file);
			std::fclose(shader_file);
			throw std::runtime_error("Couldn't fully retrieve data from shader file "s + shader_filename);
		}
		src_buf[filelen] = '\0'; //Ensure it is a null-terminated

		filters[name] = src_buf;

		std::fclose(shader_file);
	}

	if (!std::feof(file))
	{
		//An error occured while reading the next line from file
		std::fclose(file);
		throw std::runtime_error("Failure on reading lines entry from shaders.cfg");
	}

	ANDROID_LOG("Finished reading effects shaders from shaders.cfg, %lu in total fetched", filters.size());

	std::fclose(file);
	return filters;
}
