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

#include "renderer/gles2_renderer.h"

#include <mutex>

#define MAX(a,b) ((a)<(b) ? (b) : (a))

int myosd_fps;
int myosd_zoom_to_window;

//GLES2 renderer related stuff
static std::mutex gl_mutex;
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
	    std::lock_guard lock(gl_mutex);
	    
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
static gles2_renderer* gl_renderer = nullptr;

extern "C" void myosd_video_onSurfaceCreated()
{
	//Called whenever the surface is first created or recreated (Activity restart)
	//we must to setup to GL state
	if (gl_renderer != nullptr)
	{
		std::lock_guard lock(gl_mutex);

		delete gl_renderer; //destruct previous renderer object to cleanup resources
		gl_renderer = nullptr;
	}
}

static int old_width, old_height;
extern "C" void myosd_video_onDrawFrame()
{
	if (primlist)
	{
		std::lock_guard lock(gl_mutex);

		if (min_width != old_width || min_height != old_height || gl_renderer == nullptr)
		{
			old_width = min_width; old_height = min_height;

			if (gl_renderer == nullptr)
				gl_renderer = new gles2_renderer(min_width, min_height);
			else
				gl_renderer->on_viewport_change(min_width, min_height);
		}

		gl_renderer->render(*primlist);
	}
}

#if 0
//Called when configuration changed
//TODO: Need to synchronize with mame running thread, otherwise things can break
extern "C" void myosd_video_onSurfaceChange(unsigned width, unsigned height)
{
	if (!gl_renderer)
		gl_renderer = new gl_renderer(width, height);
	else
		gl_renderer->on_surface_changed(width, height);
}
#endif
