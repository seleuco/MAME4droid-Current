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
#include "screen.h"

//MYOSD headers
#include "myosd.h"
#include "window.h"

#include "modules/monitor/monitor_module.h"
#include "modules/render/render_module.h"

#include "renderer/myosd_renderer.h"
#include "renderer/gles3_renderer.h"
#include "renderer/gles1_renderer.h"

#include "myosd_platform.h"

#include <mutex>
#include <vector>
#include <string>
#include <stdexcept>

#define MAX(a,b) ((a)<(b) ? (b) : (a))

int myosd_fps;
int myosd_zoom_to_window;
//defaults to the whole target, which is the case with no artwork around
float myosd_screen_rect[4] = { 0.0f, 0.0f, 1.0f, 1.0f };

float g_hack_offscreen_overdrive = 0.0f;

// game's declared screen refresh in milli-Hz (0 = unknown); read by Java for
// frame-rate matching. Cached in update() where machine() is valid.
static int g_game_refresh_millihz = 0;

// last frame's emu WORK wall-time in ns, deposited by the update_throttle
// DAV HACK in emu/video.cpp; Java reads+clears it each frame to feed ADPF.
// Same-thread by construction; volatile future-proofs it (arm64: 64-bit
// aligned accesses are single-copy atomic) at zero cost.
volatile s64 g_myosd_frame_work_ns = 0;

// every OSD defines the one instance of this (see osdwindow.h)
osd_video_config video_config;

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

#define MYOSD_LOG(...) MYOSD_PLATFORM_LOG("GLRENDERER", __VA_ARGS__)

//===============================================================================
//	emu thread side of the GL-thread handoff (the legacy rend_mutex protocol,
//	relocated verbatim; called from myosd_window_info and renderer_myosd)
//===============================================================================

void myosd_gl_init()
{
	std::lock_guard lock(rend_mutex);

	if(my_renderer){
		my_renderer->init_renderer();
	}
}

void myosd_gl_exit()
{
	std::lock_guard lock(rend_mutex);

	// force_recreate_renderer = true;
	if(my_renderer){
		my_renderer->end_renderer();
	}

	rendering = false;
}

void myosd_gl_sync(render_primitive_list *primlist, bool in_menu, int min_width, int min_height)
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

		my_renderer->sync_state(primlist, in_menu);
	}
	rendering = true;
}

//============================================================
//  video_init
//============================================================

bool my_osd_interface::video_init()
{
	MYOSD_LOG("my_osd_interface::video_init");

	g_game_refresh_millihz = 0; // arm refresh capture for this machine

	// single virtual screen; osd_window reads this global
	video_config.windowed = 0;
	video_config.prescale = 1;
	video_config.numscreens = 1;

	myosd_gl_init();

	m_video_none = strcmp(options().video(), "none") == 0;

	if (!window_init())
		return false;

	// create our *single* window, we dont do multiple windows or monitors
	osd_window_config conf;
	auto win = std::make_unique<myosd_window_info>(
			machine(), *this, *m_render,
			m_monitor_module->pick_monitor(options(), 0), conf);
	if (win->window_init())
		return false;

	s_window_list.emplace_back(std::move(win));

	return true;
}

//============================================================
//  window_init
//============================================================

bool my_osd_interface::window_init()
{
	return true;
}

//============================================================
//  video_exit
//============================================================

void my_osd_interface::video_exit()
{
	MYOSD_LOG("my_osd_interface::video_exit");

	g_game_refresh_millihz = 0;

	window_exit();
}

//============================================================
//  window_exit
//============================================================

void my_osd_interface::window_exit()
{
	// stops the GL renderer (complete_destroy) and frees the target
	while (!s_window_list.empty())
	{
		auto window = std::move(s_window_list.back());
		s_window_list.pop_back();
		window->destroy();
	}
}

//============================================================
//  update
//============================================================

void my_osd_interface::update(bool skip_redraw)
{
    osd_printf_verbose("my_osd_interface::update\n");

    osd_common_t::update(skip_redraw);

    if(m_callbacks.video_draw == nullptr)
        return;

    bool in_game = /*machine().phase() == machine_phase::RUNNING &&*/ &(machine().system()) != &GAME_NAME(___empty);
    bool in_menu = /*machine().phase() == machine_phase::RUNNING &&*/ machine().ui().is_menu_active();
    bool running = machine().phase() == machine_phase::RUNNING;
    // capture the game's refresh once per machine (0 = not captured yet, reset
    // in video_init/video_exit); the enumerator walk runs only until a valid
    // value is found, after that this is a single int compare.
    if (in_game && running && g_game_refresh_millihz == 0) {
        screen_device *scr = screen_device_enumerator(machine().root_device()).first();
        attoseconds_t ra = scr ? scr->refresh_attoseconds() : 0;
        g_game_refresh_millihz = ra > 0 ? (int)(ATTOSECONDS_TO_HZ(ra) * 1000.0 + 0.5) : 0;
    }
    mame_machine_manager::instance()->ui().set_show_fps(myosd_fps);

    // if skipping this redraw, bail
    if (!skip_redraw && !m_video_none && !s_window_list.empty()) {
        s_window_list.front()->update();
    }

    m_callbacks.video_draw(skip_redraw || m_video_none, in_game, in_menu, running);
}

//===============================================================================
//	JNI callbacks called from GL thread (GLViewSurface.Renderer)
//===============================================================================

void myosd_video_createRenderer(int renderer, int hdr)
{
    MYOSD_LOG("create renderer %d %d",renderer, hdr);

    current_renderer = renderer;

    switch (current_renderer)
    {
        case SW_RENDERER:
            my_renderer = new gles1_renderer(render_width, render_height);
            break;

        case NATIVE_RENDERER:
            my_renderer = new gles3_renderer(render_width, render_height, hdr > 0 ? true: false, hdr > 0 ? ((float)hdr / 100.0f) : 0);
            break;
        default:
            MYOSD_LOG("Error create renderer: Renderer %d not found!", current_renderer);
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

extern "C" int myosd_video_onDrawFrame(int renderer, int hdr)
{
	myosd_renderer* current_renderer = nullptr;

    {
        std::lock_guard lock(rend_mutex);

        if (force_recreate_renderer) {
            if (my_renderer) {
                delete my_renderer;
                my_renderer = nullptr;
            }
            force_recreate_renderer = false;
        }

        if(!rendering)
            return -1;

        if(my_renderer == nullptr) {
            myosd_video_createRenderer(renderer, hdr);
        }

        current_renderer = my_renderer;
    }

    //now render async
    if (current_renderer) {
        current_renderer->render();
    }

	return 0;
}

extern "C" void myosd_video_getShaders(const char*** list, int* n)
{
	static std::vector<const char*> char_list;

    static std::vector<std::string> shaders;

	shaders = gles3_renderer::get_shaders_supported();

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

extern "C" int myosd_video_getGameRefreshMilliHz()
{
	return g_game_refresh_millihz;
}

// read-and-clear (same emu thread as the depositor): 0 = no fresh sample,
// e.g. throttle disabled - Java then falls back to the wall interval
extern "C" int64_t myosd_video_getFrameWorkNs()
{
	int64_t v = g_myosd_frame_work_ns;
	g_myosd_frame_work_ns = 0;
	return v;
}

extern "C" void myosd_video_loadShaders(const char* path)
{
    // load effect shaders; never let a shader error abort the app (missing or
    // broken shaders.cfg / shader file) - degrade to no effects
    try {
        gles3_renderer::load_shaders(path);
    }
    catch (const std::exception& e) {
        MYOSD_LOG("loadShaders failed, continuing without effects: %s", e.what());
    }
    catch (...) {
        MYOSD_LOG("loadShaders failed, continuing without effects");
    }
}

