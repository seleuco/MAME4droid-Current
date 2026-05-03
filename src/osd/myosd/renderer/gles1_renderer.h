// license:BSD-3-Clause
// copyright-holders: David Valdeita (Seleuco) & Filipe Paulino (FlykeSpice)
/***************************************************************************

    gles1_renderer.h

    GL software renderer based on GLES 1.x for MAME4droid

***************************************************************************/

#pragma once

#ifndef GLES1_RENDERER_H
#define GLES1_RENDERER_H

#include "myosd_renderer.h"
#include "render.h"

#include <GLES/gl.h>
#include <GLES/glext.h>

#include <cstdlib>

class gles1_renderer : public myosd_renderer
{
        public:
        gles1_renderer(int width, int height);

        void sync_state(const render_primitive_list* primlist) override;
        void render() override;
        void on_emulatedsize_change(int width, int height) override;

        //Shaders not supported by software renderer..
        void set_shader(const char* shader_name) override {}
        static std::vector<std::string> get_shaders_supported() { return {};}

        ~gles1_renderer() override
        {
            std::free(m_screenbuff);
            glDeleteTextures(1, &m_texture_id);
        }
        private:

        GLuint m_texture_id;
        GLfloat m_texcoords[8];

        int m_last_filter_mode = -1;
		GLint m_max_tex_size;
		bool create_texture = false;

        int m_width, m_height;
        int m_tex_width, m_tex_height;
        int m_pitch;

        void* m_screenbuff;
        };

#endif //GLES1_RENDERER_H
