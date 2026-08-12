// license:BSD-3-Clause
//============================================================
//
//  window.cpp - Android window (the single emulated surface)
//
//  MAME4DROID by David Valdeita (Seleuco)
//
//============================================================

// MAME headers
#include "emu.h"
#include "render.h"
#include "ui/uimain.h"
#include "drivenum.h"

// MYOSD headers
#include "myosd.h"
#include "window.h"

#include "modules/monitor/monitor_module.h"
#include "modules/render/render_module.h"

//============================================================
//  myosd_window_info
//============================================================

myosd_window_info::myosd_window_info(
		running_machine &machine,
		my_osd_interface &osd,
		render_module &renderprovider,
		const std::shared_ptr<osd_monitor_info> &monitor,
		const osd_window_config &config) :
	osd_window_t(machine, renderprovider, 0, monitor, config),
	m_osd(osd),
	m_min_width(0), m_min_height(0),
	m_vis_width(0), m_vis_height(0),
	m_in_menu(false)
{
}

//============================================================
//  window_init
//============================================================

int myosd_window_info::window_init()
{
	create_target();

	// keep the legacy startup view: base create_target() honours -view
	target()->set_view(0);

	renderer_create();
	if (has_renderer() && renderer().create() != 0)
		return 1;

	return 0;
}

//============================================================
//  update - runs on the emu thread each frame; sizing logic
//  relocated as-is from the legacy my_osd_interface::update
//============================================================

void myosd_window_info::update()
{
	int vis_width, vis_height;
	int min_width, min_height;

	float pixel_aspect = 1.0f;

	bool in_game = &(machine().system()) != &GAME_NAME(___empty);
	m_in_menu = machine().ui().is_menu_active();

	bool autores = myosd_display_width == 0 && myosd_display_height == 0;

	if (in_game && (myosd_zoom_to_window || autores)) {

		if (!autores) {

			target()->compute_visible_area(myosd_display_width, myosd_display_height, 1.0,
										   target()->orientation(), vis_width, vis_height);

			min_width = vis_width;
			min_height = vis_height;
		} else {

			target()->compute_minimum_size( min_width, min_height);

			if (min_width <= 0) min_width = 640;
			if (min_height <= 0) min_height = 480;
			if(min_width>640)min_width=640;
			if(min_height>480)min_height=480;

			target()->set_keepaspect(true);

			target()->compute_visible_area(min_width, min_height, 1.0,
										   target()->orientation(), vis_width, vis_height);

			target()->set_keepaspect(false);

			if (vis_height <= 0) vis_height = min_height;
			if (vis_width <= 0) vis_width = min_width;

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

		if (m_osd.callbacks().video_change != nullptr) {
			m_osd.callbacks().video_change(min_width, min_height, vis_width, vis_height);
		}

		target()->set_bounds(min_width, min_height, pixel_aspect);
	}

	// hand the live primitives to the renderer; the actual GL draw
	// happens later on the GL thread (myosd_video_onDrawFrame)
	m_primlist = &target()->get_primitives();

	m_primlist->acquire_lock();

	//Where the game screen sits inside the target, so the touch lightgun can map
	//against it instead of the whole canvas (they differ once artwork is around).
	//Re-derived every frame because MAME's own video options (zoom to screen
	//area, view) move it without changing the target size. Cheap: the loop stops
	//at the screen quad, which comes early; if a game has none we give up.
	if (in_game != m_screen_rect_in_game) {
		m_screen_rect_in_game = in_game;
		m_screen_rect_tries = 0;
		myosd_screen_rect[0] = 0.0f; myosd_screen_rect[1] = 0.0f;
		myosd_screen_rect[2] = 1.0f; myosd_screen_rect[3] = 1.0f;
	}

	if (in_game && m_min_width > 0 && m_min_height > 0 && m_screen_rect_tries < 120) {
		bool found = false;

		for (const render_primitive &prim : *m_primlist) {
			//VECTORBUF is the vector games' equivalent of the screen quad
			if (prim.type == render_primitive::QUAD &&
				(PRIMFLAG_GET_SCREENTEX(prim.flags) || PRIMFLAG_GET_VECTORBUF(prim.flags))) {
				myosd_screen_rect[0] = prim.bounds.x0 / (float)m_min_width;
				myosd_screen_rect[1] = prim.bounds.y0 / (float)m_min_height;
				myosd_screen_rect[2] = prim.bounds.x1 / (float)m_min_width;
				myosd_screen_rect[3] = prim.bounds.y1 / (float)m_min_height;
				found = true;
				break;
			}
		}

		//count only misses, so a game without a screen stops costing a full scan
		m_screen_rect_tries = found ? 0 : m_screen_rect_tries + 1;
	}

	if (has_renderer())
		renderer().draw(0);
	m_primlist->release_lock();
}

//============================================================
//  complete_destroy
//============================================================

void myosd_window_info::complete_destroy()
{
	myosd_gl_exit();
}
