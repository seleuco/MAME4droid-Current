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
#include "renderer/gles1_renderer.h"

#include <android/log.h>

#include <mutex>
#include <vector>
#include <string>

#define MAX(a,b) ((a)<(b) ? (b) : (a))

int myosd_fps;
int myosd_zoom_to_window;

//GLES renderer related stuff
enum
{
    SW_RENDERER = 1,
    NATIVE_RENDERER
};

static std::mutex rend_mutex;
//static render_primitive_list *primlist = nullptr;
static int rendering = false;
static int current_renderer = SW_RENDERER;
static myosd_renderer* my_renderer = nullptr;
static int render_width = 640, render_height = 480;
static int old_render_width, old_render_height;
static bool force_recreate_renderer = false;
static std::string current_shader_name = "";

#define ANDROID_LOG(...) __android_log_print(ANDROID_LOG_DEBUG, "GLRENDERER", __VA_ARGS__)

//============================================================
//  video_init
//============================================================

void my_osd_interface::video_init()
{
	ANDROID_LOG("my_osd_interface::video_init");
	
	{
       std::lock_guard lock(rend_mutex);

	   if(my_renderer){
		   my_renderer->init_renderer();
	   }
		   
    }

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
	ANDROID_LOG("my_osd_interface::video_exit");

	{
        std::lock_guard lock(rend_mutex);

       // force_recreate_renderer = true;
	   if(my_renderer){
		   my_renderer->end_renderer();
	   }
	   
	   rendering = false;
		   
    }

    // free the render target
    machine().render().target_free(m_target);
    m_target = nullptr;

    if (m_callbacks.video_exit != nullptr)
        m_callbacks.video_exit();
}

//============================================================
//  update
//============================================================

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
        int min_width, min_height;

		float pixel_aspect = 1.0f;

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

				float display_aspect = (float)vis_width / (float)vis_height;
                float texture_aspect = (float)min_width / (float)min_height;
                pixel_aspect = display_aspect / texture_aspect;
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

            target()->set_bounds(min_width, min_height, pixel_aspect);
        }

            render_primitive_list *local_primlist = &target()->get_primitives();

            local_primlist->acquire_lock();
            {
                std::lock_guard lock(rend_mutex);
                render_width = min_width;
                render_height = min_height;

                if (my_renderer)
				{
					if (render_width != old_render_width || render_height != old_render_height)
					{
						old_render_width = render_width;
						old_render_height = render_height;

						my_renderer->on_emulatedsize_change(render_width, render_height);
					}

                    my_renderer->sync_state(local_primlist);
                }
				rendering = true;
            }
            local_primlist->release_lock();
    }

    m_callbacks.video_draw(skip_redraw || m_video_none, in_game, in_menu, running);
}

//===============================================================================
//	JNI callbacks called from GL thread (GLViewSurface.Renderer)
//===============================================================================

void myosd_video_createRenderer(int renderer)
{
    ANDROID_LOG("create renderer %d",renderer);

    current_renderer = renderer;

    switch (current_renderer)
    {
        case SW_RENDERER:
            my_renderer = new gles1_renderer(render_width, render_height);
            break;

        case NATIVE_RENDERER:
            my_renderer = new gles2_renderer(render_width, render_height);
            break;
        default:
            ANDROID_LOG("Error create renderer: Renderer %d not found!", current_renderer);
            // Safety fallback: load the software renderer by default to prevent crashes
            my_renderer = new gles1_renderer(render_width, render_height);
            current_renderer = SW_RENDERER; // Sync the state variable
            break;
    }

	if (current_renderer == NATIVE_RENDERER && my_renderer != nullptr && !current_shader_name.empty()) {
        try {
            my_renderer->set_shader(current_shader_name.c_str());
        } catch (...) {
            my_renderer->set_shader(nullptr);
            current_shader_name = "";
        }
    }
    old_render_width = render_width;
    old_render_height = render_height;
}

extern "C" void myosd_video_newRenderer()
{
	std::lock_guard lock(rend_mutex);
    force_recreate_renderer = true;
}

extern "C" int myosd_video_onDrawFrame(int renderer)
{
	std::lock_guard lock(rend_mutex);

    if (force_recreate_renderer) {
        if (my_renderer) {
            delete my_renderer; // Safe: We are in GLThread
            my_renderer = nullptr;
        }
        force_recreate_renderer = false;
    }
	
	if(!rendering)
		return -1;

    if(my_renderer == nullptr) {
        myosd_video_createRenderer(renderer); // Safe: we are in GLThread
    }

	my_renderer->render();

    return 0;
}

extern "C" void myosd_video_getShaders(const char*** list, int* n)
{
	static std::vector<const char*> char_list;

    static std::vector<std::string> shaders;

	shaders = gles2_renderer::get_shaders_supported();

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

	if (shader_name != nullptr) {
        current_shader_name = shader_name;
    } else {
        current_shader_name = "";
    }

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
    // load effect shaders for gles2 renderer
    gles2_renderer::load_shaders(path);
}

