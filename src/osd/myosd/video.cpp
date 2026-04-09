// license:BSD-3-Clause
//============================================================
//
//  video.cpp -  osd video handling
//
//  MAME4DROID by David Valdeita (Seleuco)
//
//============================================================

// MAME headers
#include "emu.h"
#include "render.h"
#include "rendlay.h"
#include "ui/uimain.h"
#include "ui/ui.h"
#include "mame.h"
#include "ui/menu.h"

#include "drivenum.h"

//MYOSD headers
#include "myosd.h"

#include "renderer/myosd_renderer.h"
#include "renderer/gles2_renderer.h"
#include "renderer/gles2_software.h"

#include <mutex>
#include <vector>

#define MAX(a,b) ((a)<(b) ? (b) : (a))

int myosd_fps;
int myosd_zoom_to_window;

//GLES2 renderer related stuff
static std::mutex rend_mutex;
static render_primitive_list *primlist = nullptr;

//============================================================
//  video_init
//============================================================

void my_osd_interface::video_init()
{
    osd_printf_verbose("my_osd_interface::video_init\n");

    // create our *single* render target, we dont do multiple windows or monitors
    m_target = machine().render().target_alloc();

    m_video_none = strcmp(options().value(OPTION_VIDEO), "none") == 0;

    m_min_width = 0;
    m_min_height = 0;
    m_vis_width = 0;
    m_vis_height = 0;
}

//============================================================
//  video_exit
//============================================================

void my_osd_interface::video_exit()
{
    osd_printf_verbose("my_osd_interface::video_exit\n");

    // free the render target
    machine().render().target_free(m_target);
    m_target = nullptr;

    if (m_callbacks.video_exit != nullptr)
        m_callbacks.video_exit();
}


//============================================================
//  update
//============================================================

//FlykeSpice: Need to hoist these variables here so they are used by GL renderer callbacks down below
static int min_width=640, min_height=480;

void my_osd_interface::update(bool skip_redraw)
{
    osd_printf_verbose("my_osd_interface::update\n");

    if(m_callbacks.video_draw == nullptr)
        return;

    bool in_game = /*machine().phase() == machine_phase::RUNNING &&*/ &(machine().system()) != &GAME_NAME(___empty);
    bool in_menu = /*machine().phase() == machine_phase::RUNNING &&*/ machine().ui().is_menu_active();
    bool running = machine().phase() == machine_phase::RUNNING;
    mame_machine_manager::instance()->ui().set_show_fps(myosd_fps);

    // if skipping this redraw, bail
    if (!skip_redraw && !m_video_none) {

        int vis_width, vis_height;

        //__android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "video min_width:%d min_height:%d",min_width,min_height);

        //target()->compute_visible_area(MAX(640,myosd_display_width), MAX(480,myosd_display_height), 1.0, target()->orientation(), vis_width, vis_height);

        bool autores = myosd_display_width == 0 && myosd_display_height == 0;

        if (in_game && (myosd_zoom_to_window || autores)) {

            if (!autores) {

                target()->compute_visible_area(myosd_display_width, myosd_display_height, 1.0,
                                               target()->orientation(), vis_width, vis_height);

                min_width = vis_width;
                min_height = vis_height;
            } else {

                target()->compute_minimum_size( min_width, min_height);
                if(min_width>640)min_width=640;
                if(min_height>480)min_height=480;

                target()->set_keepaspect(true);

                target()->compute_visible_area(min_width, min_height, 1.0,
                                               target()->orientation(), vis_width, vis_height);

                target()->set_keepaspect(false);

            }

        } else {
            if (in_game) {
                min_width = vis_width = myosd_display_width;
                min_height = vis_height = myosd_display_height;
            } else {
                min_width = vis_width = myosd_display_width_osd;
                min_height = vis_height = myosd_display_height_osd;
            }
        }

        // check for a change in the min-size of render target *or* size of the vis screen
        if (min_width != m_min_width || min_height != m_min_height
             || vis_width != m_vis_width || vis_height != m_vis_height) {

            m_min_width = min_width;
            m_min_height = min_height;
            m_vis_width = vis_width;
            m_vis_height = vis_height;

            if (m_callbacks.video_change != nullptr) {
                m_callbacks.video_change(min_width, min_height, vis_width, vis_height);
            }

	    target()->set_bounds(min_width, min_height);
        }
    }

    if (!skip_redraw)
    {
	    std::lock_guard lock(rend_mutex);
	    
	    if (primlist)
		    primlist->release_lock();

	    primlist = &target()->get_primitives();
	    primlist->acquire_lock();
    }

    m_callbacks.video_draw(skip_redraw || m_video_none, in_game, in_menu, running);
}

//===============================================================================
//	JNI callbacks called from GL thread (GLViewSurface.Renderer)
//===============================================================================
enum
{
	SOFTWARE_RENDERER = 1,
	GLES2_RENDERER
};

static myosd_renderer* my_renderer = nullptr;
static int current_renderer = SOFTWARE_RENDERER;

extern "C" void myosd_video_onChooseRenderer(int renderer)
{
	std::lock_guard lock(rend_mutex);
	current_renderer = renderer;

	if (my_renderer)
		delete my_renderer;

	switch (current_renderer)
	{
		case SOFTWARE_RENDERER:
			my_renderer = new gles2_software(min_width, min_height);
		break;

		case GLES2_RENDERER:
			my_renderer = new gles2_renderer(min_width, min_height);
		break;
	}
}

extern "C" void myosd_video_onSurfaceCreated()
{
	//Called whenever the surface is first created or recreated (Activity restart)
	//The current GL context is invalidated, so we need to setup it again.
	myosd_video_onChooseRenderer(current_renderer);
}

static int old_width, old_height;
extern "C" void myosd_video_onDrawFrame()
{
	std::lock_guard lock(rend_mutex);

	if (my_renderer && primlist)
	{
		if (min_width != old_width || min_height != old_height)
		{
			old_width = min_width; old_height = min_height;

			my_renderer->on_emulatedsize_change(min_width, min_height);
		}

		my_renderer->render(*primlist);
	}
}

extern "C" void myosd_video_getShaders(int renderer, const char*** list, int* n)
{
	static std::vector<const char*> char_list;

	std::vector<std::string> shaders;
	switch (renderer)
	{
		case SOFTWARE_RENDERER:
			shaders = gles2_software::get_shaders_supported();
		break;

		case GLES2_RENDERER:
			shaders = gles2_renderer::get_shaders_supported();
		break;
	}

	if (shaders.size() > 0)
	{
		char_list.clear();
		for (const std::string& shader : shaders)
		{
			char_list.push_back(shader.c_str());
		}

		*list = char_list.data();
		*n = char_list.size();
	}
	else
	{
		*list = nullptr; *n = 0;
	}
}

extern "C" bool myosd_video_setShader(const char* shader_name)
{
	std::lock_guard lock(rend_mutex);

	try
	{
		if (my_renderer)
			my_renderer->set_shader(shader_name);
	}
	catch (...)
	{
		//Error occured when loading shader, revert to no effect shader mode and warn the caller
		my_renderer->set_shader(nullptr);
		return false;
	}

	return true;
}

extern "C" void myosd_video_loadShaders(const char* path)
{
    //FlykeSpice: load effect shaders for gles2 renderer
    gles2_renderer::load_shaders(path);
}
