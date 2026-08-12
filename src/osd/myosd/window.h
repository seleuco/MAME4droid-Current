// license:BSD-3-Clause
//============================================================
//
//  window.h - Android window (the single emulated surface)
//
//  MAME4DROID by David Valdeita (Seleuco)
//
//============================================================

#ifndef MAME_OSD_MYOSD_WINDOW_H
#define MAME_OSD_MYOSD_WINDOW_H

#pragma once

#include "modules/osdwindow.h"

#include <memory>

class my_osd_interface;
class render_module;
class osd_monitor_info;

// no native handle here: Java owns the real surface and the GL
// renderer attaches to it from the GL thread via JNI (video.cpp)
class myosd_window_info : public osd_window_t<void *>
{
public:
	myosd_window_info(
			running_machine &machine,
			my_osd_interface &osd,
			render_module &renderprovider,
			const std::shared_ptr<osd_monitor_info> &monitor,
			const osd_window_config &config);

	int window_init();

	// osd_window overridables
	virtual osd_dim get_size() override { return osd_dim(m_vis_width, m_vis_height); }
	virtual void capture_pointer() override { }
	virtual void release_pointer() override { }
	virtual void show_pointer() override { }
	virtual void hide_pointer() override { }
	virtual void update() override;
	virtual void complete_destroy() override;

	// consumed by renderer_myosd (drawmyosd.cpp) to feed the GL bridge
	int min_width() const { return m_min_width; }
	int min_height() const { return m_min_height; }
	bool in_menu() const { return m_in_menu; }

private:
	my_osd_interface &m_osd;

	int m_min_width, m_min_height;
	int m_vis_width, m_vis_height;

	//screen rect lookup state (see update())
	bool m_screen_rect_in_game = false;
	int m_screen_rect_tries = 0;
	bool m_in_menu;
};

// emu-thread side of the GL-thread handoff, implemented in video.cpp
void myosd_gl_init();
void myosd_gl_exit();
void myosd_gl_sync(render_primitive_list *primlist, bool in_menu, int min_width, int min_height);

#endif // MAME_OSD_MYOSD_WINDOW_H
