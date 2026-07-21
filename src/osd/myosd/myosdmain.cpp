// license:BSD-3-Clause
//============================================================
//
//  myosdmain.cpp - main file for my OSD
//
//  MAME4DROID by David Valdeita (Seleuco)
//
//============================================================

#include <functional>   // only for oslog callback

// standard includes
#include <unistd.h>
#include <chrono>       // pointer click/hold tracking

// MAME headers
#include "osdepend.h"
#include "emu.h"
#include "emuopts.h"
#include "main.h"
#include "fileio.h"
#include "gamedrv.h"
#include "drivenum.h"
#include "romload.h"
#include "screen.h"
#include "softlist_dev.h"
#include "strconv.h"
#include "corestr.h"

#include "uiinput.h"
#include "render.h"

#include "modules/lib/osdobj_common.h"
#include "modules/osdwindow.h"

// OSD headers
#include "video.h"
#include "myosd.h"
#include "myosd_netplay.h"

#include "myosd_platform.h"
#include <cstring>

#define MIN(a,b) ((a)<(b) ? (a) : (b))
#define MAX(a,b) ((a)<(b) ? (b) : (a))

//============================================================
// MYOSD globals
//============================================================
int myosd_display_width;
int myosd_display_height;
int myosd_display_width_osd;
int myosd_display_height_osd;
int myosd_bitmap_filtering;
int myosd_vector_improved;

//============================================================
//  myosd_main
//============================================================

my_osd_interface *osdInterface = nullptr;

extern "C" int myosd_main(int argc, char** argv, myosd_callbacks* callbacks, size_t callbacks_size)
{
    myosd_callbacks host_callbacks;
    memset(&host_callbacks, 0, sizeof(host_callbacks));
    memcpy(&host_callbacks, callbacks, MIN(sizeof(host_callbacks), sizeof(myosd_callbacks)));

    if (argc == 0) {
        static const char* args[] = {"myosd"};
        argc = 1;
        argv = (char**)args;
    }

    std::vector<std::string> args = osd_get_command_line(argc, argv);

    // SDL/Windows-style startup: typed options + register_options so the
    // module manager knows the myosd/droid providers (myosdopts.cpp)
    myosd_options options;
    osdInterface = new my_osd_interface(options, host_callbacks);
    osdInterface->register_options();
    int res = emulator_info::start_frontend(options, *osdInterface, args);
    delete osdInterface;
    osdInterface = nullptr;
    return res;
}

extern "C" void myosd_pause(bool pause)
{
    if(osdInterface!= nullptr && osdInterface->isMachine() )
    {
        pause ? osdInterface->machine().pause() : osdInterface->machine().resume();
    }
}

extern "C" bool myosd_is_paused()
{
    if(osdInterface!= nullptr && osdInterface->isMachine())
    {
        return osdInterface->machine().paused();
    }
    return false;
}

extern "C" void myosd_speed_hack()
{
    int cpu_overclock = 100;
    if (osdInterface != nullptr && osdInterface->isMachine()) {
        device_enumerator iter(osdInterface->machine().root_device());
        for (device_t &device: iter) {
            if (dynamic_cast<cpu_device *>(&device) != nullptr) {
                cpu_device *firstcpu = downcast<cpu_device *>(&device);

                std::string name = std::string(firstcpu->name());
                MYOSD_PLATFORM_LOG("hacks", "hacking %s", name.c_str());
                if (name.find("R4600") != std::string::npos || name.find("TMS34010") != std::string::npos) {
                    cpu_overclock = 60;
                }
                else if (name.find("SH-2") != std::string::npos)
                {
                    //cpu_overclock = 55;
                    cpu_overclock = 90;//now we have DRC
                }
                else
                {
                    cpu_overclock = 90;
                }
                firstcpu->set_clock_scale((float) cpu_overclock * 0.01f);
                MYOSD_PLATFORM_LOG("hacks", "hacked to %d", cpu_overclock);
                break;
            }
        }
    }
}

//============================================================
//  myosd_pushEvent
//============================================================
extern "C" void myosd_pushEvent(myosd_inputevent event)
{
    /* Click tracking: clicks stay positive while the
     * press can still be a click/tap and are NEGATED once it becomes a
     * hold or drag (>250ms or moved too far). The UI relies on this:
     * exactly 1 activates arrows, 2 activates items, <0 means drag. */
    static unsigned buttons = 0;
    static bool touch_down = false;
    static int mouse_clicks = 0, touch_clicks = 0;
    static int mouse_px = 0, mouse_py = 0, touch_px = 0, touch_py = 0;
    static std::chrono::steady_clock::time_point mouse_t0, touch_t0;

    if(osdInterface!= nullptr && osdInterface->isMachine() && osdInterface->target()!= nullptr) {

        /* CLICK/TAP_DISTANCE are squared desktop pixels;
         * scale them to the current target size (~240p = 1x). */
        int const th = osdInterface->target()->height();
        int const unit = MAX(1, (th > 0 ? th : 480) / 240);
        int const click_dist = 16 * unit * unit;
        int const tap_dist = 49 * unit * unit;
        auto const now = std::chrono::steady_clock::now();
        auto const hold_time = std::chrono::milliseconds(250);

        // negate the click count once the press stops being a click/tap
        auto check_hold_drag = [&](int &clicks, int px, int py, int maxdist,
                std::chrono::steady_clock::time_point t0) {
            if (0 < clicks) {
                int const dx = event.data.pointer_data.x - px;
                int const dy = event.data.pointer_data.y - py;
                if (((t0 + hold_time) < now) || (maxdist < ((dx * dx) + (dy * dy))))
                    clicks = -clicks;
            }
        };

        switch(event.type) {
            case event.MYOSD_KEY_EVENT:
                osdInterface->target()->push_char_event(event.data.key_char);
                break;
            case event.MYOSD_MOUSE_MOVE_EVENT:
            {
                if (buttons & 1)
                    check_hold_drag(mouse_clicks, mouse_px, mouse_py, click_dist, mouse_t0);
                osdInterface->target()->push_pointer_update(
                        osd::ui_event_handler::pointer::MOUSE,
                        0,
                        1,
                        event.data.pointer_data.x, event.data.pointer_data.y,
                        buttons, 0, 0, mouse_clicks);
                break;
            }
            case event.MYOSD_MOUSE_BT1_DOWN:
            {
                unsigned pressed = (1) << 0;
                buttons |= pressed;
                mouse_clicks = event.data.pointer_data.double_action ? 2 : 1;
                mouse_px = event.data.pointer_data.x;
                mouse_py = event.data.pointer_data.y;
                mouse_t0 = now;
                osdInterface->target()->push_pointer_update(
                        osd::ui_event_handler::pointer::MOUSE,
                        0,
                        1,
                        event.data.pointer_data.x, event.data.pointer_data.y,
                        buttons, pressed, 0, mouse_clicks);
                break;
            }
            case event.MYOSD_MOUSE_BT1_UP:
            {
                unsigned released = (1) << 0;
                buttons &= ~released;
                check_hold_drag(mouse_clicks, mouse_px, mouse_py, click_dist, mouse_t0);
                osdInterface->target()->push_pointer_update(
                        osd::ui_event_handler::pointer::MOUSE,
                        0,
                        1,
                        event.data.pointer_data.x, event.data.pointer_data.y,
                        buttons, 0, released, mouse_clicks);
                break;
            }
            case event.MYOSD_MOUSE_BT2_DOWN: {
                unsigned pressed = (1) << 1;
                buttons |= pressed;
                osdInterface->target()->push_pointer_update(
                        osd::ui_event_handler::pointer::MOUSE,
                        0,
                        1,
                        event.data.pointer_data.x, event.data.pointer_data.y,
                        buttons, pressed, 0, mouse_clicks);

                break;
            }
            case event.MYOSD_MOUSE_BT2_UP: {
                unsigned released = (1) << 1;
                buttons &= ~released;
                osdInterface->target()->push_pointer_update(
                        osd::ui_event_handler::pointer::MOUSE,
                        0,
                        1,
                        event.data.pointer_data.x, event.data.pointer_data.y,
                        buttons, 0, released, mouse_clicks);

                break;
            }
            case event.MYOSD_FINGER_MOVE:
            {
                // Java can emit MOVE before DOWN when it anchors a finger
                // (or when one slides off a virtual control): drop those
                if (!touch_down)
                    break;
                check_hold_drag(touch_clicks, touch_px, touch_py, tap_dist, touch_t0);
                osdInterface->target()->push_pointer_update(
                        osd::ui_event_handler::pointer::TOUCH,
                        1,
                        1,
                        event.data.pointer_data.x, event.data.pointer_data.y,
                        1, 0, 0, touch_clicks);
                break;
            }
            case event.MYOSD_FINGER_DOWN:
            {
                if (touch_down)
                    break;
                touch_down = true;
                touch_clicks = event.data.pointer_data.double_action ? 2 : 1;
                touch_px = event.data.pointer_data.x;
                touch_py = event.data.pointer_data.y;
                touch_t0 = now;
                osdInterface->target()->push_pointer_update(
                        osd::ui_event_handler::pointer::TOUCH,
                        1,
                        1,
                        event.data.pointer_data.x, event.data.pointer_data.y,
                        1, 1, 0, touch_clicks);

                break;
            }
            case event.MYOSD_FINGER_UP:
            {
                if (!touch_down)
                    break;
                touch_down = false;
                check_hold_drag(touch_clicks, touch_px, touch_py, tap_dist, touch_t0);
                osdInterface->target()->push_pointer_update(
                        osd::ui_event_handler::pointer::TOUCH,
                        1,
                        1,
                        event.data.pointer_data.x, event.data.pointer_data.y,
                        0, 0, 1, touch_clicks);
                /* the finger is gone: release first (update), then leave with
                 * released=0 or menu.cpp asserts released == its button state */
                osdInterface->target()->push_pointer_leave(
                        osd::ui_event_handler::pointer::TOUCH,
                        1,
                        1,
                        event.data.pointer_data.x, event.data.pointer_data.y,
                        0, touch_clicks);
                break;
            }
            default:
                osd_printf_error("has unknown myosd event type (%u)\n",event.type);
        }

    }
}

//============================================================
//  myosd_get
//============================================================
extern "C" intptr_t myosd_get(int var)
{
    switch (var)
    {
        case MYOSD_MAME_VERSION:
            return (int)(atof(emulator_info::get_bare_build_version()) * 1000.0);

        case MYOSD_MAME_VERSION_STRING:
            return (intptr_t)(void*)emulator_info::get_build_version();

        case MYOSD_BITMAP_FILTERING:
            return myosd_bitmap_filtering;

        case MYOSD_VECTOR_IMPROVED:
            return myosd_vector_improved;

        case MYOSD_FPS:
            return myosd_fps;

        case MYOSD_SPEED:
            return 0;
    }
    return 0;
}

//============================================================
//  myosd_set
//============================================================
extern "C" void myosd_set(int var, intptr_t value)
{
    switch (var)
    {
        case MYOSD_DISPLAY_WIDTH:
            myosd_display_width = value;
            break;
        case MYOSD_DISPLAY_HEIGHT:
            myosd_display_height = value;
            break;
        case MYOSD_DISPLAY_WIDTH_OSD:
            myosd_display_width_osd = value;
            break;
        case MYOSD_DISPLAY_HEIGHT_OSD:
            myosd_display_height_osd = value;
            break;
        case MYOSD_BITMAP_FILTERING:
            myosd_bitmap_filtering = value;
            break;
        case MYOSD_VECTOR_IMPROVED:
            myosd_vector_improved = value;
            break;
        case MYOSD_FPS:
            myosd_fps = value;
            break;
        case MYOSD_ZOOM_TO_WINDOW:
            myosd_zoom_to_window = value;
            break;
        case MYOSD_SPEED:
            //myosd_speed = value;
            break;
        case MYOSD_VERSION:
            emulator_info::myosd_droid_version = (char*)(void*)value;
            break;
    }
}

//============================================================
//  constructor
//============================================================

my_osd_interface::my_osd_interface(myosd_options &options, myosd_callbacks &callbacks)
: osd_common_t(options), m_machine(nullptr), m_options(options),
  m_video_none(0), m_sample_rate(0), m_callbacks(callbacks)
{
    // osd_common_t already did osd_output::push(this)

    if (m_callbacks.output_init != NULL)
        m_callbacks.output_init();
}


//============================================================
//  destructor
//============================================================

my_osd_interface::~my_osd_interface()
{
    if (m_callbacks.output_exit != NULL)
        m_callbacks.output_exit();

    // osd_common_t does osd_output::pop(this)
}

//-------------------------------------------------
//  output_callback  - callback for osd_printf_...
//-------------------------------------------------

void my_osd_interface::output_callback(osd_output_channel channel, const util::format_argument_pack<char> &args)
{
    if (channel == OSD_OUTPUT_CHANNEL_VERBOSE && !verbose())
        return;

    std::ostringstream buffer;
    util::stream_format(buffer, args);

    if (m_callbacks.output_text != NULL)
    {
        _Static_assert((int)MYOSD_OUTPUT_ERROR == (int)OSD_OUTPUT_CHANNEL_ERROR);
        _Static_assert((int)MYOSD_OUTPUT_WARNING == (int)OSD_OUTPUT_CHANNEL_WARNING);
        _Static_assert((int)MYOSD_OUTPUT_INFO == (int)OSD_OUTPUT_CHANNEL_INFO);
        _Static_assert((int)MYOSD_OUTPUT_DEBUG == (int)OSD_OUTPUT_CHANNEL_DEBUG);
        _Static_assert((int)MYOSD_OUTPUT_VERBOSE == (int)OSD_OUTPUT_CHANNEL_VERBOSE);

        m_callbacks.output_text(channel, buffer.str().c_str());
    }
    else
    {
        static const char* channel_str[] = {"[ERROR]: ", "[WARN]: ", "[INFO]: ", "[DEBUG]: ", "[VERBOSE]: ", "[LOG]: "};
        fputs(channel_str[channel], stderr);
        fputs(buffer.str().c_str(), stderr);
    }
}

//============================================================
// get_game_info - convert game_driver to a myosd_game_info
//============================================================
static void get_game_info(myosd_game_info* info, const game_driver *driver, running_machine &machine)
{
    memset(info, 0, sizeof(myosd_game_info));

    /*
    device_type                 type;               // static type info for driver class
    const char *                parent;             // if this is a clone, the name of the parent
    const char *                year;               // year the game was released
    const char *                manufacturer;       // manufacturer of the game
    machine_creator_wrapper     machine_creator;    // machine driver tokens
    ioport_constructor          ipt;                // pointer to constructor for input ports
    driver_init_wrapper         driver_init;        // DRIVER_INIT callback
    const tiny_rom_entry *      rom;                // pointer to list of ROMs for the game
    const char *                compatible_with;
    const internal_layout *     default_layout;     // default internally defined layout
    machine_flags::type         flags;              // orientation and other flags; see defines above
    char                        name[MAX_DRIVER_NAME_CHARS + 1]; // short name of the game
    */

    //
    // MAME does not do device types anymore! (MAME 0.252+)
    //
    // what we are going to do is assume if a machine has no software list or media then it is an ARCADE
    // if it has keyboard input then it is a COMPUTER
    //
    info->type = MYOSD_GAME_TYPE_ARCADE;

    /*
    int type = (driver->flags & machine_flags::MASK_TYPE);

    if (type == MACHINE_TYPE_ARCADE)
        info->type = MYOSD_GAME_TYPE_ARCADE;
    else if (type == MACHINE_TYPE_CONSOLE)
        info->type = MYOSD_GAME_TYPE_CONSOLE;
    else if (type == MACHINE_TYPE_COMPUTER)
        info->type = MYOSD_GAME_TYPE_COMPUTER;
    else
        info->type = MYOSD_GAME_TYPE_OTHER;
    */

    info->source_file  = driver->type.source();
    info->parent       = driver->parent;
    info->name         = driver->name;
    info->description  = driver->type.fullname();
    info->year         = driver->year;
    info->manufacturer = driver->manufacturer;

    if (info->parent != NULL && info->parent[0] == '0' && info->parent[1] == 0)
        info->parent = "";

    if (driver->flags & (MACHINE_NOT_WORKING|MACHINE_UNEMULATED_PROTECTION))
        info->flags |= MYOSD_GAME_INFO_NOT_WORKING;

    if ((driver->flags & machine_flags::MASK_ORIENTATION) == machine_flags::ROT90 || (driver->flags & machine_flags::MASK_ORIENTATION) == machine_flags::ROT270)
        info->flags |= MYOSD_GAME_INFO_VERTICAL;

    if (driver->flags & MACHINE_IS_BIOS_ROOT)
        info->flags |= MYOSD_GAME_INFO_BIOS;

    if (driver->flags & (MACHINE_WRONG_COLORS | MACHINE_IMPERFECT_COLORS | MACHINE_IMPERFECT_GRAPHICS | MACHINE_NO_COCKTAIL))
        info->flags |= MYOSD_GAME_INFO_IMPERFECT_GRAPHICS;

    if (driver->flags & (MACHINE_NO_SOUND | MACHINE_IMPERFECT_SOUND | MACHINE_NO_SOUND_HW))
        info->flags |= MYOSD_GAME_INFO_IMPERFECT_SOUND;

    if (driver->flags & MACHINE_SUPPORTS_SAVE)
        info->flags |= MYOSD_GAME_INFO_SUPPORTS_SAVE;

    // check for a vector or LCD screen
    machine_config config(*driver, machine.options());
    for (const screen_device &device : screen_device_enumerator(config.root_device()))
    {
        if (device.screen_type() == SCREEN_TYPE_VECTOR)
            info->flags |= MYOSD_GAME_INFO_VECTOR;
        if (device.screen_type() == SCREEN_TYPE_LCD)
            info->flags |= MYOSD_GAME_INFO_LCD;
    }

    // get software lists for this system
    {
        static std::unordered_set<std::string> g_software;
        std::string software;

        software_list_device_enumerator swlistdev_iter(config.root_device());
        for (software_list_device &swlistdev : swlistdev_iter)
        {
            if (software.size() != 0)
                software.append(",");
            software.append(swlistdev.list_name());
        }

        // get all the file extensions for cart/flop/etc
        image_interface_enumerator img_iter(config.root_device());
        for (device_image_interface &img : img_iter)
        {
            // ignore things not user loadable
            if (!img.user_loadable())
                continue;

            osd_printf_debug("MEDIA: %s[%s]: '%s' (%s)%s\n", driver->name, img.brief_instance_name(), img.image_type_name(), img.file_extensions(), (img.must_be_loaded() ? "*" : ""));

            std::string media_type = img.brief_instance_name();

            // get the extensions and add them too as <type>:<extension> (ie cart:a26 or flop:t64)
            std::string extensions(img.file_extensions());
            for (int start = 0, end = extensions.find_first_of(',');; start = end + 1, end = extensions.find_first_of(',', start))
            {
                std::string curext(extensions, start, (end == -1) ? extensions.length() - start : end - start);

                if (curext.size() != 0) {
                    char ach[64];
                    snprintf(ach, sizeof(ach), "%s:%s", media_type.c_str(), curext.c_str());

                    if (software.size() != 0)
                        software.append(",");
                    software.append(ach);
                }

                if (end == -1)
                    break;
            }
        }

        if (software.size() != 0)
        {
            osd_printf_debug("SOFTWARE: '%s'\n", software.c_str());
            info->software_list = g_software.insert(software).first->c_str();
        }
    }

    if (info->software_list != NULL && info->software_list[0] != 0) {
        info->type = MYOSD_GAME_TYPE_CONSOLE;

        if (false)
            info->type = MYOSD_GAME_TYPE_COMPUTER;
    }
}

//============================================================
// get_romless_machines
// read (or create) romless.ini
//============================================================
static std::vector<std::string> get_romless_machines(running_machine &machine)
{
    std::vector<std::string> list;
    char line[256];
    char version[256];

    snprintf(version, sizeof(version), "; MAME %s\n", emulator_info::get_bare_build_version());
    FILE* file = fopen("romless.ini", "r");
    if (file == NULL || fgets(line, sizeof(line), file) == NULL || strcmp(line, version) != 0)
    {
        std::size_t const total = driver_list::total();

        fclose(file);
        file = fopen("romless.ini", "w");
        fputs(version, file);
        fputs("[romless machines]\n", file);

        osd_printf_debug("FIND ROMLESS MACHINES...\n");
        osd_ticks_t time = osd_ticks();

        // iterate over all machines and find romless machines
        for (int i = 0; i < total; i++)
        {
            game_driver const &driver(driver_list::driver(i));
            machine_config config(driver, machine.options());

            if (&driver == &GAME_NAME(___empty))
                continue;

            if (driver.flags & (MACHINE_NOT_WORKING | MACHINE_NO_SOUND | MACHINE_IS_INCOMPLETE | MACHINE_NO_SOUND_HW | MACHINE_MECHANICAL))
                continue;

            int num_roms = 0;
            for (device_t const &device : device_enumerator(config.root_device()))
            {
                for (const rom_entry *region = rom_first_region(device); region; region = rom_next_region(region))
                {
                    for (const rom_entry *rom = rom_first_file(region); rom; rom = rom_next_file(rom))
                    {
                        num_roms++;
                    }
                }
            }
            if (num_roms == 0)
            {
                fprintf(file, "%s\n", driver.name);
            }
        }

        time = osd_ticks() - time;
        osd_printf_debug("FIND ROMLESS MACHINES... took %0.3fsec\n", (float)time / osd_ticks_per_second());

        fclose(file);
        file = fopen("romless.ini", "r");
    }

    while (fgets(line, sizeof(line), file) != NULL)
    {
        if (line[strlen(line) - 1] == '\n') line[strlen(line) - 1] = '\0';
        if (line[strlen(line) - 1] == '\r') line[strlen(line) - 1] = '\0';

        if (line[0] == '\0' || line[0] == ';')
            continue;

        if (line[0] == '[')
            continue;

        list.push_back(line);
    }

    fclose(file);
    return list;
 }

//============================================================
// get_game_list - get list of available games
//============================================================
static std::vector<myosd_game_info> get_game_list(running_machine &machine)
{
    // this is the same code, and method, used by selgame.cpp
    std::size_t const total = driver_list::total();
    std::vector<bool> included(total, false);

    // iterate over ROM directories and look for potential ROMs
    file_enumerator path(machine.options().media_path());
    for (osd::directory::entry const *dir = path.next(); dir; dir = path.next())
    {
        char drivername[64];
        char *dst = drivername;
        char const *src;

        // build a name for it
        for (src = dir->name; *src != 0 && *src != '.' && dst < &drivername[std::size(drivername) - 1]; ++src)
            *dst++ = tolower(uint8_t(*src));

        *dst = 0;
        int const drivnum = driver_list::find(drivername);
        if (0 <= drivnum)
            included[drivnum] = true;
    }

    // add romless machines too.
    auto romless = get_romless_machines(machine);
    for(const auto& drivername: romless)
    {
        int const drivnum = driver_list::find(drivername.c_str());
        if (0 <= drivnum)
            included[drivnum] = true;
    }

    // now build a list of just avail games, as myosd_game_info(s)
    std::vector<myosd_game_info> list;
    for (int i = 0; i < total; i++)
    {
        game_driver const &driver(driver_list::driver(i));
        if (included[i]) {
            myosd_game_info info;
            get_game_info(&info, &driver, machine);
            list.push_back(info);
        }
    }
    return list;
}

//============================================================
//  init
//============================================================

void my_osd_interface::init(running_machine &machine)
{
    // This function is responsible for initializing the OSD-specific
    // video and input functionality, and registering that functionality
    // with the MAME core.
    //
    // In terms of video, this function is expected to create one or more
    // render_targets that will be used by the MAME core to provide graphics
    // data to the system. Although it is possible to do this later, the
    // assumption in the MAME core is that the user interface will be
    // visible starting at init() time, so you will have some work to
    // do to avoid these assumptions.
    //
    // In terms of input, this function is expected to enumerate all input
    // devices available and describe them to the MAME core by adding
    // input devices and their attached items (buttons/axes) via the input
    // system.
    //
    // Beyond these core responsibilities, init() should also initialize
    // any other OSD systems that require information about the current
    // running_machine.
    //
    // This callback is also the last opportunity to adjust the options
    // before they are consumed by the rest of the core.
    //
    // shadow the machine pointer: isMachine() keeps its legacy semantics
    m_machine = &machine;

    // base: stores machine, registers the EXIT notifier (-> our osd_exit),
    // reads -verbose and sets up the optional watchdog
    osd_common_t::init(machine);

    if (DebugLog > 1)
        set_verbose(true);

    auto &options = this->options();

    // determine if we are benchmarking, and adjust options appropriately
    int bench = options.bench();
    if (bench > 0)
    {
        options.set_value(OPTION_THROTTLE, false, OPTION_PRIORITY_MAXIMUM);
        options.set_value(OSDOPTION_SOUND, "none", OPTION_PRIORITY_MAXIMUM);
        options.set_value(OSDOPTION_VIDEO, "none", OPTION_PRIORITY_MAXIMUM);
        options.set_value(OPTION_SECONDS_TO_RUN, bench, OPTION_PRIORITY_MAXIMUM);
    }

    // check for HISCORE
    if (options.hiscore())
    {
        // ...NOTE hiscores are handled via plugins
    }

    // check for OPTION_BEAM and map to OPTION_BEAM_WIDTH_MIN and MAX
    float beam = options.beam();
    if (beam != 1.0)
    {
        options.set_value(OPTION_BEAM_WIDTH_MIN, beam, OPTION_PRIORITY_CMDLINE);
        options.set_value(OPTION_BEAM_WIDTH_MAX, beam, OPTION_PRIORITY_CMDLINE);
    }

    /* get number of processors */
    const char *nump = options.numprocessors();

    osd_num_processors = 0; // 0 is Auto

    if (strcmp(nump, "auto") != 0)
    {
        osd_num_processors = atoi(nump);
        if (osd_num_processors < 1)
        {
            osd_printf_warning("numprocessors < 1 doesn't make much sense. Assuming auto ...\n");
            osd_num_processors = 0;
        }
    }

    // audio: 0 means silent, the legacy contract kept by no_sound()
    m_sample_rate = options.sample_rate();
    if (strcmp(options.sound(), "none") == 0)
        m_sample_rate = 0;

    // clear input state so the profile lazy-init in input_update() runs
    memset(&g_input, 0, sizeof(g_input));

    // select and start all the modules: monitor, render (video_init),
    // input, font, sound, debugger, midi, netdev and output
    init_subsystems();

    bool in_game = (&m_machine->system() != &GAME_NAME(___empty));

    if (m_callbacks.game_init != NULL && in_game)
    {
        myosd_game_info info;
        get_game_info(&info, &machine.system(), machine);
        m_callbacks.game_init(&info);
    }
    else if (m_callbacks.game_list != NULL && !in_game)
    {
        std::vector<myosd_game_info> list = get_game_list(machine);
        m_callbacks.game_list(list.data(), list.size());
    }
}

//============================================================
//  osd_exit (MACHINE_NOTIFY_EXIT, registered by osd_common_t::init)
//============================================================

void my_osd_interface::osd_exit()
{
    bool in_game = (&m_machine->system() != &GAME_NAME(___empty));

    if (m_callbacks.game_exit != NULL && in_game)
        m_callbacks.game_exit();

    // legacy host-visible teardown order: renderer/target first, then
    // the video/input/sound exit callbacks, then the module manager
    window_exit();

    if (m_callbacks.video_exit != nullptr)
        m_callbacks.video_exit();

    if (m_callbacks.input_exit != nullptr)
        m_callbacks.input_exit();

    if (m_sample_rate != 0 && m_callbacks.sound_exit != NULL)
        m_callbacks.sound_exit();

    osd_common_t::osd_exit();

    /* Rollback cleanup: free all captured ram_states and reset flags.    */
    myosd_netplay_state_cleanup();
    myosd_netplay_set_ff_active(0);
    myosd_netplay_set_audio_mute(0);
}

//============================================================
//  target - the single window's render target (nullable
//  between sessions, myosd_pushEvent relies on that)
//============================================================

render_target *my_osd_interface::target() const
{
    return s_window_list.empty() ? nullptr : s_window_list.front()->target();
}

//============================================================
//  no_sound - legacy contract: rate 0 (or -sound none) is silent
//============================================================

bool my_osd_interface::no_sound()
{
    return m_sample_rate == 0;
}

