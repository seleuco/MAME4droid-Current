// license:BSD-3-Clause
// copyright-holders:David Valdeita (Seleuco) & Filipe Paulino (FlykeSpice)
/***************************************************************************

    filter_shader.cpp

    Effect shaders for MAME4droid's GLES2 renderer

***************************************************************************/


#include "filter_shader.h"
#include "gl_utils.hxx"
#include "gles2_renderer.h"

#include <android/log.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <stdexcept>
#include <vector>
#include <utility>

#define ANDROID_LOG(...) __android_log_print(ANDROID_LOG_DEBUG, "filter_shader", __VA_ARGS__)

void filter_shader::load_filter(const std::string& filter_src, bool linear)
{
    m_linear = linear;
    if (m_program)
	{ 
		glDeleteProgram(m_program); 
		m_program = 0; 
	}

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

    GLint last_program;
    glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
    glUseProgram(m_program);

    auto texture_sampler = glGetUniformLocation(m_program, "Texture");
    glUniform1i(texture_sampler, 0); 

    glUseProgram(last_program);
    m_ortho_dirty = true;
}

void filter_shader::set_ortho(std::array<float, 4*4> ortho)
{
    m_ortho = ortho;
    m_ortho_dirty = true;
}

void filter_shader::draw_quad(GLuint texture_id, const float* verts, const float* uv, int tex_w, int tex_h, int view_w, int view_h)
{
    if (!m_program) return;
	
    GLint last_program;
    glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
    
    glUseProgram(m_program);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    if (m_ortho_dirty) {
        glUniformMatrix4fv(m_uniform_MVPMatrix, 1, GL_FALSE, m_ortho.data());
        m_ortho_dirty = false;
    }

    glUniform2f(m_uniform_TextureSize, tex_w, tex_h);
    glUniform2f(m_uniform_InputSize,   tex_w, tex_h);
    glUniform2f(m_uniform_OutputSize, view_w, view_h);

	if (m_uniform_FrameCount != -1) glUniform1i(m_uniform_FrameCount, ++m_framecount);

    glVertexAttribPointer(gles2_renderer::ATTRIB_POSITION, 2, GL_FLOAT, GL_FALSE, 0, verts);
    glEnableVertexAttribArray(gles2_renderer::ATTRIB_POSITION);

    glVertexAttribPointer(gles2_renderer::ATTRIB_TEXUV, 2, GL_FLOAT, GL_FALSE, 0, uv);
    glEnableVertexAttribArray(gles2_renderer::ATTRIB_TEXUV);

    static const GLubyte indices[] = { 0, 1, 2, 0, 2, 3 };
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, indices);
    
    glUseProgram(last_program);
}

std::vector<std::pair<std::string, filter_data>> filter_shader::load_filters(const std::string &root_path)
{
	using namespace std::string_literals;

    std::vector<std::pair<std::string, filter_data>> filters;
	std::string path = root_path + "shaders.cfg";
	std::string src_buf;

	ANDROID_LOG("Opening %s...", path.c_str());
	std::FILE* file = std::fopen(path.c_str(), "rb");

	if (!file) throw std::runtime_error("Failure on opening shaders.cfg");

	char buf[150];
	while (std::fgets(buf, sizeof buf, file))
	{
		if (std::strlen(buf) <= 10 || buf[0] == '#') continue;

		char shader_filename[100], name[100], linear[10];
		int version;

        int ret = std::sscanf(buf, "%99[^;];%99[^;];%9[^;];%d", shader_filename, name, linear, &version);
		if (ret < 4)
		{
			std::fclose(file);
			throw std::runtime_error("Error extracting line fields with scanf, expected 4 but only matched "s + std::to_string(ret));
		}

		if (version != 1)
			continue;

		path = root_path + "shaders/" + shader_filename;
		std::FILE* shader_file = std::fopen(path.c_str(), "rb");
		if (!shader_file)
		{
			std::fclose(file); 
			throw std::runtime_error("error opening shader file "s + shader_filename);
		}

		std::fseek(shader_file, 0, SEEK_END);
		auto filelen = std::ftell(shader_file);
		std::rewind(shader_file);

        src_buf.resize(filelen);

		if (std::fread(src_buf.data(), 1, filelen, shader_file) != filelen)
		{
			std::fclose(file); 
			std::fclose(shader_file);
			throw std::runtime_error("Couldn't fully retrieve data from shader file "s + shader_filename);
		}

        std::string lin_str(linear);
        bool is_linear = (lin_str == "true" || lin_str == "1" || lin_str == "linear" || lin_str == "LINEAR");

        filters.push_back({name, {src_buf, is_linear}});

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
