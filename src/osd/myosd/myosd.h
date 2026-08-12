// license:BSD-3-Clause
//============================================================
//
//  myosd.h -  OSD header
//
//  MAME4DROID  by David Valdeita (Seleuco)
//
//============================================================

#ifndef _myosd_h_
#define _myosd_h_

#include "modules/lib/osdobj_common.h"
#include "modules/osdmodule.h"
#include "modules/font/font_module.h"
#include "../frontend/mame/ui/menuitem.h"

#include "myosdopts.h"
#include "myosd_core.h"

//============================================================
// DebugLog
//============================================================
#define DebugLog 1
#if DebugLog == 0
#define osd_printf_debug(...) (void)0
#endif
#if DebugLog <= 1
#define osd_printf_verbose(...) (void)0
#endif

//============================================================
// MYOSD globals
//============================================================
extern int myosd_display_width;
extern int myosd_display_height;
extern int myosd_display_width_osd;
extern int myosd_display_height_osd;
extern int myosd_fps;
extern int myosd_zoom_to_window;
//screen quad inside the render target, normalized 0..1 (x0,y0,x1,y1)
extern float myosd_screen_rect[4];

// input state shared with the input modules and myosd_netplay.cpp
extern myosd_input_state g_input;

//============================================================
//  TYPE DEFINITIONS
//============================================================

// platform contract: each port implements this (droid/droid_font.cpp)
osd_font::ptr myosd_platform_font_alloc();

class my_osd_interface : public osd_common_t
{
public:
	// construction/destruction
	my_osd_interface(myosd_options &options, myosd_callbacks &callbacks);
	virtual ~my_osd_interface();

    // general overridables
    virtual void init(running_machine &machine) override;
    virtual void update(bool skip_redraw) override;
    virtual void input_update(bool relative_reset) override;
    virtual void check_osd_inputs() override;

    // input overridables
    virtual void customize_input_type_list(std::vector<input_type_entry> &typelist) override;

    // video overridables (called by osd_common_t::init_subsystems/exit_subsystems)
    virtual bool video_init() override;
    virtual bool window_init() override;
    virtual void video_exit() override;
    virtual void window_exit() override;

    // audio: keep the legacy "no configured rate means silent" contract
    virtual bool no_sound() override;

    // osd_common_t plumbing (no event loop of our own; Java drives us)
    virtual void process_events() override { }
    virtual bool has_focus() const override { return true; }
    virtual void osd_exit() override;
    virtual myosd_options &options() override { return m_options; }

    // osd_output
    virtual void output_callback(osd_output_channel channel, const util::format_argument_pack<char> &args) override;

    // getters
    bool isMachine() {return m_machine!=nullptr;}
    running_machine &machine() const { assert(m_machine != nullptr); return *m_machine; }
    render_target *target() const;
    myosd_callbacks &callbacks() { return m_callbacks; }
    int sample_rate() const { return m_sample_rate; }

private:
    // internal state; shadows the (private) base pointer so isMachine()
    // keeps its legacy never-cleared semantics between sessions
    running_machine *m_machine;
    myosd_options &m_options;

    // video
    int m_video_none;

    // audio
    int m_sample_rate;

    // host app callbacks
    myosd_callbacks m_callbacks;
};

//============================================================
//  work.cpp
//============================================================

extern int osd_num_processors;

#endif
