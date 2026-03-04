// license:BSD-3-Clause
//============================================================
//
//  myosd-droid.cpp - Implementation of osd droid stuff
//
//  MAME4DROID  by David Valdeita (Seleuco)
//
//============================================================

#include "myosd-droid.h"
#include "opensl_snd.h"


#include <unistd.h>
#include <fcntl.h>

#include <android/log.h>
#include <android/keycodes.h>

#include <pthread.h>

#include <sys/stat.h>

#include <string>
#include <chrono>
#include <sstream>
#include <vector>

#include "myosd_core.h"
#include "myosd_saf.h"

#include "../../../android-MAME4droid/app/src/main/jni/com_seleuco_mame4droid_Emulator.h"

#define MIN(a,b) ((a)<(b) ? (a) : (b))
#define MAX(a,b) ((a)<(b) ? (b) : (a))

static int lib_inited = 0;

static int myosd_droid_inMenu = 1;
static int myosd_droid_inGame = 0;
static int myosd_droid_running = 0;

//video
static int myosd_droid_video_width = 1;
static int myosd_droid_video_height = 1;
static int myosd_droid_resolution = 1;
static int myosd_droid_resolution_osd = 1;
static int myosd_droid_res_width = 1;
static int myosd_droid_res_height = 1;
static int myosd_droid_res_width_osd = 1;
static int myosd_droid_res_height_osd = 1;
static int myosd_droid_res_width_native = 1;
static int myosd_droid_res_height_native = 1;

//input - save
static int myosd_droid_num_buttons = 0;
static int myosd_droid_light_gun = 0;;
static int myosd_droid_mouse = 0;
static int myosd_droid_num_of_joys = 1;
static int myosd_droid_num_ways = 8;
static int myosd_droid_pxasp1 = 0;
//static int myosd_droid_service = 0;
static int myosd_droid_exitGame = 0;
static int myosd_droid_mouse_enable = 1;
static int myosd_droid_keyboard_enable = 1;

//input data
static unsigned long joy_status[MYOSD_NUM_JOY];

static float joy_analog_x[MYOSD_NUM_JOY];
static float joy_analog_y[MYOSD_NUM_JOY];
static float joy_analog_trigger_x[MYOSD_NUM_JOY];
static float joy_analog_trigger_y[MYOSD_NUM_JOY];

static float lightgun_x[MYOSD_NUM_GUN];
static float lightgun_y[MYOSD_NUM_GUN];

static float mouse_x[MYOSD_NUM_MICE];
static float mouse_y[MYOSD_NUM_MICE];
static int mouse_status[MYOSD_NUM_MICE];
static float cur_x_mouse = 0;
static float cur_y_mouse = 0;

#define ANDROID_NUM_KEYS 256
static uint8_t keyboard[MYOSD_NUM_KEYS];
static int android_to_mame_key[ANDROID_NUM_KEYS];

static pthread_mutex_t mouse_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t pause_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t pause_update_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  pause_update_var   = PTHREAD_COND_INITIALIZER;

//saveload
static int myosd_droid_savestate = 0;
static int myosd_droid_loadstate = 0;
static int myosd_droid_autosave = 0;
static std::string myosd_droid_statepath;

//misc
static int myosd_droid_warn_on_exit = 1;
static int myosd_droid_show_fps = 1;
static int myosd_droid_zoom_to_window = 1;
static std::string myosd_droid_selected_game;
static std::string myosd_droid_rom_name;
static std::string myosd_droid_cli_params;
static std::string myosd_droid_overlay_effect;
static int myosd_droid_auto_frameskip = 0;
static int myosd_droid_cheats = 0;
static int myosd_droid_skip_gameinfo = 0;
static int myosd_droid_disable_drc = 1;
static int myosd_droid_enable_drc_use_c = 1;
static int myosd_droid_simple_ui = 0;
static int myosd_droid_pause = 0;
static int myosd_droid_do_pause = 0;
static int myosd_droid_do_resume = 0;
static int myosd_droid_is_paused_in_update = 0;
static int myosd_droid_doing_paused_in_update = 0;
static int myosd_droid_is_paused_in_emu = 0;

static int myosd_num_processors = -1;
static int myosd_droid_no_dzsat = 0;
static int myosd_speed_hacks = 0;
static int myosd_droid_init_game = 0;

static int myosd_plugin_autofire = 0;
static int myosd_plugin_inputmacro = 0;
static int myosd_plugin_hiscore = 0;

//vector options
static int myosd_droid_vector_beam2x = 1;
static int myosd_droid_vector_flicker = 0;

//SAF
int myosd_droid_using_saf = 0;
//int myosd_droid_reload = 1;
int myosd_droid_savestatesinrompath = 0;
std::string myosd_droid_safpath;
int myosd_droid_using_mameini=1;

//sound
static int soundInit = 0;
static int sound_engine = 1;
static int myosd_droid_sound_value = 48000;
static int myosd_droid_sound_frames = 1024;
static OPENSL_SND *opensl_snd_ptr  = nullptr;

//Android callbacks
static void (*dumpVideo_callback)(void) = nullptr;

static void
(*changeVideo_callback)(int newWidth, int newHeight, int newVisWidth, int newVisHeight) = nullptr;

static void (*openSound_callback)(int rate, int stereo) = nullptr;

static void (*dumpSound_callback)(void *buffer, int size) = nullptr;

static void (*closeSound_callback)(void) = nullptr;

static void (*initInput_callback)(void) = nullptr;

static int (*safOpenFile_callback)(const char *pathName,const char *mode) = nullptr;

static int (*safReadDir_callback)(const char *dirName, int reload) = nullptr;

static char **(*safGetNextDirEntry_callback)(int dirId) = nullptr;

static void (*safCloseDir_callback)(int dirId) = nullptr;
//end android callbacks

#define PIXEL_PITCH  4

void myosd_droid_setVideoCallbacks(
        void (*dump_video_java)(void),
        void (*change_video_java)(int, int, int, int)) {

    __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "setVideoCallbacks");

    dumpVideo_callback = dump_video_java;
    changeVideo_callback = change_video_java;
}

void myosd_droid_setAudioCallbacks(
        void (*open_sound_java)(int, int),
        void (*dump_sound_java)(void *, int),
        void (*close_sound_java)(void)) {

    __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "setAudioCallbacks");

    openSound_callback = open_sound_java;
    dumpSound_callback = dump_sound_java;
    closeSound_callback = close_sound_java;
}

void myosd_droid_setInputCallbacks(
        void (*init_input_java)(void)) {

    __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "setInputCallbacks");

    initInput_callback = init_input_java;
}

void myosd_droid_setSAFCallbacks(
        int (*safOpenFile_java)(const char *, const char *),
        int (*safReadDir_java)(const char *, int reload),
        char **(*safGetNextDirEntry_java)(int),
        void (*safCloseDir_java)(int)
) {

    __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "setSAFCallbacks");

    safOpenFile_callback = safOpenFile_java;
    safReadDir_callback = safReadDir_java;
    safGetNextDirEntry_callback = safGetNextDirEntry_java;
    safCloseDir_callback = safCloseDir_java;
}


void myosd_droid_initMyOSD(const char *path, int nativeWidth, int nativeHeight) {

    __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "initMyOSD path: %s nativeWidth: %d nativeHeight: %d", path,
                        nativeWidth, nativeHeight);

    /*ret = */chdir(path);

    myosd_droid_res_width_native = nativeWidth;
    myosd_droid_res_height_native = nativeHeight;
}

static void droid_myosd_check_pause(void){

    pthread_mutex_lock( &pause_update_mutex );

    while(myosd_droid_is_paused_in_update)
    {
        myosd_droid_doing_paused_in_update = 1;
        pthread_cond_wait( &pause_update_var, &pause_update_mutex );
    }
    myosd_droid_doing_paused_in_update = 0;

    pthread_mutex_unlock( &pause_update_mutex );
}

static void droid_change_pause_in_update(int value){

    pthread_mutex_lock( &pause_update_mutex );

    myosd_droid_is_paused_in_update = value;

    if(!myosd_droid_is_paused_in_update)
        pthread_cond_signal( &pause_update_var );

    pthread_mutex_unlock( &pause_update_mutex );

    if(myosd_droid_is_paused_in_update){
        auto const now = std::chrono::steady_clock::now();
        auto const max = std::chrono::milliseconds(1000)+now;
        while (!myosd_droid_doing_paused_in_update && std::chrono::steady_clock::now() < max) {
            usleep(1);
        };
    }
}

static void droid_change_pause_in_emu(int value){

    if (value) {
        myosd_droid_do_pause = 1;
        myosd_droid_is_paused_in_emu = 1;
        auto const now = std::chrono::steady_clock::now();
        auto const max = std::chrono::milliseconds(1000)+now;
        while (!myosd_is_paused() && std::chrono::steady_clock::now() < max) {
            usleep(1);
        };
    } else {
        myosd_droid_do_resume = 1;
        auto const now = std::chrono::steady_clock::now();
        auto const max = std::chrono::milliseconds(1000)+now;
        while (myosd_is_paused() && std::chrono::steady_clock::now() < max) {
            usleep(1);
        };
        myosd_droid_is_paused_in_emu = 0;
    }
}

static void droid_pause(int doPause) {

    pthread_mutex_lock(&pause_mutex);

    __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "doPause %d", doPause);

    int isPaused = (myosd_is_paused() ? 1 : 0);

    if ((myosd_droid_inGame && myosd_droid_running && !myosd_droid_is_paused_in_update && !isPaused)
        || myosd_droid_is_paused_in_emu
            ) {

        __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "doPause in emu %d...", doPause);

        droid_change_pause_in_emu(doPause);

    } else {
        __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "doPause in update %d...",doPause);

        droid_change_pause_in_update(doPause);
    }

    myosd_droid_pause = doPause;
    __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "doPause pausing %d...", doPause);

    pthread_mutex_unlock(&pause_mutex);
}

void myosd_droid_setMyValue(int key, int i, int value) {
    //__android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "setMyValue  %d,%d:%d",key,i,value);
    switch (key) {
        case com_seleuco_mame4droid_Emulator_SHOW_FPS:
            myosd_droid_show_fps = value;
            break;
        case com_seleuco_mame4droid_Emulator_ZOOM_TO_WINDOW:
            myosd_droid_zoom_to_window = value;
            break;
        case com_seleuco_mame4droid_Emulator_AUTO_FRAMESKIP:
            myosd_droid_auto_frameskip = value;
            break;
        case com_seleuco_mame4droid_Emulator_CHEATS:
            myosd_droid_cheats = value;
            break;
        case com_seleuco_mame4droid_Emulator_SKIP_GAMEINFO:
            myosd_droid_skip_gameinfo = value;
            break;
        case com_seleuco_mame4droid_Emulator_DISABLE_DRC:
            myosd_droid_disable_drc = value;
            break;
        case com_seleuco_mame4droid_Emulator_DRC_USE_C:
            myosd_droid_enable_drc_use_c = value;
            break;
        case com_seleuco_mame4droid_Emulator_SIMPLE_UI:
            myosd_droid_simple_ui = value;
            break;
        case com_seleuco_mame4droid_Emulator_EXIT_GAME:
            myosd_droid_exitGame = value;
            keyboard[MYOSD_KEY_ESC] = value ? 0x80: 0;
            //keyboard[MYOSD_KEY_UIMODE] = value ? 0x80: 0;
            break;
        case com_seleuco_mame4droid_Emulator_PAUSE: {
            droid_pause(value);
            break;
        }
        case com_seleuco_mame4droid_Emulator_SOUND_VALUE:
            myosd_droid_sound_value = value;
            break;
        case com_seleuco_mame4droid_Emulator_AUTOSAVE:
            myosd_droid_autosave = value;
            break;
        case com_seleuco_mame4droid_Emulator_SAVESTATE:
            keyboard[MYOSD_KEY_F6] = value ? 0x80: 0;
            //keyboard[MYOSD_KEY_LSHIFT] = value ? 0x80: 0;
            myosd_droid_savestate = value;
            break;
        case com_seleuco_mame4droid_Emulator_LOADSTATE:
            keyboard[MYOSD_KEY_F7] = value ? 0x80: 0;
            myosd_droid_loadstate = value;
            break;
        case com_seleuco_mame4droid_Emulator_EMU_RESOLUTION:
            myosd_droid_resolution = value;
            break;
        case com_seleuco_mame4droid_Emulator_OSD_RESOLUTION:
            myosd_droid_resolution_osd = value;
            break;
        case com_seleuco_mame4droid_Emulator_PXASP1:
            myosd_droid_pxasp1 = value;
            break;
        case com_seleuco_mame4droid_Emulator_VBEAM2X:
            myosd_droid_vector_beam2x = value;
            break;
        case com_seleuco_mame4droid_Emulator_VFLICKER:
            myosd_droid_vector_flicker = value;
            break;
        case com_seleuco_mame4droid_Emulator_SOUND_OPTIMAL_FRAMES:
            myosd_droid_sound_frames = value;
            break;
        case com_seleuco_mame4droid_Emulator_SOUND_OPTIMAL_SAMPLERATE: {
            if (myosd_droid_sound_value != -1 && sound_engine == 2) //en el caso de opensl siempre optimal frames
                myosd_droid_sound_value = value;
            break;
        }
        case com_seleuco_mame4droid_Emulator_SOUND_ENGINE:
            sound_engine = value;
            break;
        case com_seleuco_mame4droid_Emulator_USING_SAF:
            myosd_droid_using_saf = value;
            break;
        case com_seleuco_mame4droid_Emulator_SAVESATES_IN_ROM_PATH:
            myosd_droid_savestatesinrompath = value;
            break;
        case com_seleuco_mame4droid_Emulator_WARN_ON_EXIT:
            myosd_droid_warn_on_exit = value;
            break;
        case com_seleuco_mame4droid_Emulator_MOUSE:
            myosd_droid_mouse_enable = value;
            break;
        case com_seleuco_mame4droid_Emulator_KEYBOARD:
            myosd_droid_keyboard_enable = value;
            break;
        case com_seleuco_mame4droid_Emulator_NUM_PROCESSORS:
            myosd_num_processors = value;
            break;
        case com_seleuco_mame4droid_Emulator_NODEADZONEANDSAT:
            myosd_droid_no_dzsat = value;
            break;
        case com_seleuco_mame4droid_Emulator_MAMEINI:
            myosd_droid_using_mameini = value;
            break;
        case com_seleuco_mame4droid_Emulator_SPEED_HACKS:
            myosd_speed_hacks = value;
            break;
        case com_seleuco_mame4droid_Emulator_AUTOFIRE:
            myosd_plugin_autofire = value;
            break;
        case com_seleuco_mame4droid_Emulator_INPUTMACRO:
            myosd_plugin_inputmacro = value;
            break;
        case com_seleuco_mame4droid_Emulator_HISCORE:
            myosd_plugin_hiscore = value;
            break;
    }
}

int myosd_droid_getMyValue(int key, int i) {
    //__android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "getMyValue  %d,%d",key,i);

    if (i == 0) {
        switch (key) {
            case com_seleuco_mame4droid_Emulator_IN_MENU :
                return myosd_droid_inMenu;
            case com_seleuco_mame4droid_Emulator_IN_GAME:
                return myosd_droid_inGame;
            case com_seleuco_mame4droid_Emulator_NUMBTNS:
                return myosd_droid_num_buttons;
            case com_seleuco_mame4droid_Emulator_NUMWAYS:
                return myosd_droid_num_ways;
            case com_seleuco_mame4droid_Emulator_IS_LIGHTGUN:
                return myosd_droid_light_gun;
            case com_seleuco_mame4droid_Emulator_IS_MOUSE:
                return myosd_droid_mouse;
            case com_seleuco_mame4droid_Emulator_PAUSE:
                return myosd_is_paused() ? 1 : 0;
            default :
                return -1;
        }
    }
    return -1;
}

void myosd_droid_setMyValueStr(int key, int i, const char *value) {
    //__android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "setMyValueStr  %d,%d:%s",key,i,value);
    switch (key) {
        case com_seleuco_mame4droid_Emulator_SAF_PATH: {
            myosd_droid_safpath = std::string(value);
            break;
        }
        case com_seleuco_mame4droid_Emulator_ROM_NAME: {
            myosd_droid_rom_name = std::string(value);
            break;
        }
        case com_seleuco_mame4droid_Emulator_VERSION: {
            myosd_set(MYOSD_VERSION, (intptr_t)(void*)value);
            break;
        }
        case com_seleuco_mame4droid_Emulator_OVERLAY_EFECT: {
            myosd_droid_overlay_effect = std::string(value);
            break;
        }
        case com_seleuco_mame4droid_Emulator_CLI_PARAMS: {
            myosd_droid_cli_params = std::string(value);
            break;
        }
        default:;
    }
}

char *myosd_droid_getMyValueStr(int key, int i) {
    //__android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "getMyValueStr  %d,%d",key,i);
    switch (key) {
        case com_seleuco_mame4droid_Emulator_ROM_NAME:
            return (char*)myosd_droid_rom_name.c_str();
        case com_seleuco_mame4droid_Emulator_MAME_VERSION:
            return (char*)myosd_get(MYOSD_MAME_VERSION_STRING);
        default:
            return nullptr;
    }
    return nullptr;
}

void myosd_droid_setDigitalData(int i, unsigned long value) {

    if(i>=MYOSD_NUM_JOY) return;

    //__android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "setDigitalData  %d,%ld",i,value);

    if (i == 1 && (value & MYOSD_SELECT) && myosd_droid_num_of_joys < 2)
        myosd_droid_num_of_joys = 2;
    else if (i == 2 && (value & MYOSD_SELECT) && myosd_droid_num_of_joys < 3)
        myosd_droid_num_of_joys = 3;
    else if (i == 3 && (value & MYOSD_SELECT) && myosd_droid_num_of_joys < 4)
        myosd_droid_num_of_joys = 4;

    if(myosd_droid_pxasp1 && !myosd_droid_inMenu && myosd_droid_num_of_joys<=1)
    {
        for(int j=0; j<MYOSD_NUM_JOY;j++)
            joy_status[j] = value;
    }
    else
    {
        joy_status[i] = value;
    }

    //__android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "set_pad %ld",value);
}

void myosd_droid_setAnalogData(int i, float v1, float v2) {

    if(i>8) return;

    if(i == 8) {
        lightgun_x[0] = v1;
        lightgun_y[0] = v2;
    } else {
        if(myosd_droid_pxasp1 && !myosd_droid_inMenu && myosd_droid_num_of_joys<=1)
        {
            for(int j=0; j<MYOSD_NUM_JOY;j++)
            {
                joy_analog_x[j] = v1;
                joy_analog_y[j] = v2;
                joy_analog_trigger_x[j-4]=v1;
                joy_analog_trigger_y[j-4]=v2;
            };
        }
        else if(i<4)
        {
            joy_analog_x[i] = v1;
            joy_analog_y[i] = v2;
        }
        else
        {
            joy_analog_trigger_x[i-4]=v1;
            joy_analog_trigger_y[i-4]=v2;
        }
    }
    //__android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "set analog %d %f %f",i,v1,v2);
}

int myosd_droid_setKeyData(int keyCode,int keyAction,char keyChar) {

    //__android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "set keyData %d %d %c",keyCode,keyAction,keyChar);

    if(keyAction == com_seleuco_mame4droid_Emulator_KEY_DOWN) {
        myosd_inputevent ev;
        ev.type = ev.MYOSD_KEY_EVENT;
        switch (keyCode) {
            case AKEYCODE_DEL: keyChar = 8;break;
            case AKEYCODE_ENTER : keyChar = 13;break;
            case AKEYCODE_TAB : keyChar = 9;break;
            //TODO Escape
        }
        ev.data.key_char = keyChar;
        myosd_pushEvent(ev);
    }

    int mame_key = -1;

    if(keyCode < ANDROID_NUM_KEYS)
        mame_key = android_to_mame_key[keyCode];

    //__android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "mame_key %d",mame_key);

    if(mame_key!=-1)
        keyboard[mame_key] = keyAction == com_seleuco_mame4droid_Emulator_KEY_DOWN ? 0x80 : 0x00;

    return mame_key != -1;
}

int myosd_droid_setMouseData(int i, int mouseAction, int button, float cx, float cy) {

/*
    __android_log_print(ANDROID_LOG_DEBUG,
                        "MAME4droid.so", "set mouseData %d %d %d %f %f -> %d %d",i, mouseAction,
                        button, cx, cy, (int)cur_x_mouse, (int)cur_y_mouse);
*/
    if(i>4) return 1;

    static bool bt1_double_click = false;

    if(mouseAction == com_seleuco_mame4droid_Emulator_MOUSE_MOVE)
    {
        //__android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "MOUSEB MOUSE MOVE!!!!");

        cur_x_mouse = MAX(0, MIN(cx + cur_x_mouse, myosd_droid_video_width));
        cur_y_mouse = MAX(0, MIN(cy + cur_y_mouse, myosd_droid_video_height));

        myosd_inputevent ev;
        ev.type = ev.MYOSD_MOUSE_MOVE_EVENT;
        ev.data.pointer_data.x = cur_x_mouse;
        ev.data.pointer_data.y = cur_y_mouse;
        myosd_pushEvent(ev);

        if(!myosd_droid_inMenu) {
            pthread_mutex_lock(&mouse_mutex);

            mouse_x[i] += cx;
            mouse_y[i] += cy;

            pthread_mutex_unlock(&mouse_mutex);
       }
    }
    else if(mouseAction == com_seleuco_mame4droid_Emulator_MOUSE_MOVE_POINTER)
    {
        cur_x_mouse = cx;
        cur_y_mouse = cy;

        myosd_inputevent ev;
        ev.type = ev.MYOSD_MOUSE_MOVE_EVENT;
        ev.data.pointer_data.x = cur_x_mouse;
        ev.data.pointer_data.y = cur_y_mouse;
        myosd_pushEvent(ev);
    }
    else if(mouseAction == com_seleuco_mame4droid_Emulator_MOUSE_BTN_DOWN)
    {
        myosd_inputevent ev;
        ev.data.pointer_data.x = cur_x_mouse;
        ev.data.pointer_data.y = cur_y_mouse;

        if(button==1) {

            static float last_click_x = 0;
            static float last_click_y = 0;
            static std::chrono::steady_clock::time_point last_click_time = std::chrono::steady_clock::time_point::min();

            auto double_click_speed = std::chrono::milliseconds(250);
            auto const click = std::chrono::steady_clock::now();

            int offsetX = 4;
            int offsetY = 4;
            if(cx != -1 && cy != -1) {
                offsetX = cx;
                offsetY = cy;
                double_click_speed = std::chrono::milliseconds(400);
            }

            if (click < (last_click_time + double_click_speed)
                && (cur_x_mouse >= (last_click_x - offsetX) && cur_x_mouse <= (last_click_x + offsetX))
                && (cur_y_mouse >= (last_click_y - offsetY) && cur_y_mouse <= (last_click_y + offsetY)))
            {
                last_click_time = std::chrono::time_point<std::chrono::steady_clock>::min();
                bt1_double_click = true;
                __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "MOUSEB PULSO BT1 DBLCLK!!!!");
            }
            else
            {
                last_click_time = click;
                last_click_x = cur_x_mouse;
                last_click_y = cur_y_mouse;
            }

            ev.type = ev.MYOSD_MOUSE_BT1_DOWN;
            ev.data.pointer_data.double_action = bt1_double_click;
            myosd_pushEvent(ev);

            mouse_status[i] |= MYOSD_A;
            __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "MOUSEB PULSO BT1!");
        }
        else if(button == 2)
        {
            ev.type = ev.MYOSD_MOUSE_BT2_DOWN;
            myosd_pushEvent(ev);
            mouse_status[i] |= MYOSD_B;
        }
        else if(button == 3)
        {
            mouse_status[i] |= MYOSD_C;
        }
    }
    else if(mouseAction == com_seleuco_mame4droid_Emulator_MOUSE_BTN_UP)
    {
        myosd_inputevent ev;
        ev.data.pointer_data.x = cur_x_mouse;
        ev.data.pointer_data.y = cur_y_mouse;

        if(button==1)
        {
            ev.type = ev.MYOSD_MOUSE_BT1_UP;
            ev.data.pointer_data.double_action = bt1_double_click;
            bt1_double_click = false;
            myosd_pushEvent(ev);
            mouse_status[i] &= ~MYOSD_A;
        }
        else if(button==2)
        {
            ev.type = ev.MYOSD_MOUSE_BT2_UP;
            myosd_pushEvent(ev);
            mouse_status[i] &= ~MYOSD_B;
        }
        else if(button==3)
        {
            mouse_status[i] &= ~MYOSD_C;
        }
    }

    return 1;
}

int myosd_droid_setTouchData(int i, int touchAction,  float cx, float cy) {
/*
    __android_log_print(ANDROID_LOG_DEBUG,
                        "MAME4droid.so", "set touchData %d %d %f %f -> %d %d",i, touchAction,
                        cx, cy, (int)cur_x_mouse, (int)cur_y_mouse);
*/
    static bool double_tap = false;
    static float last_cx;
    static float last_cy;

    if(touchAction == com_seleuco_mame4droid_Emulator_FINGER_MOVE)
    {
        if(last_cx != cx || last_cy != cy) {
            myosd_inputevent ev;
            ev.type = ev.MYOSD_FINGER_MOVE;
            ev.data.pointer_data.x = cx;
            ev.data.pointer_data.y = cy;
            myosd_pushEvent(ev);
            last_cx = cx;
            last_cy = cy;
        }

    } else if (touchAction == com_seleuco_mame4droid_Emulator_FINGER_DOWN) {
        myosd_inputevent ev;
        ev.data.pointer_data.x = cx;
        ev.data.pointer_data.y = cy;

        static float last_click_x = 0;
        static float last_click_y = 0;
        static std::chrono::steady_clock::time_point last_click_time = std::chrono::steady_clock::time_point::min();

        auto double_click_speed = std::chrono::milliseconds(250);
        auto const click = std::chrono::steady_clock::now();

        int offsetX = 4;
        int offsetY = 4;
        if (cx != -1 && cy != -1) {
            offsetX = cx;
            offsetY = cy;
            double_click_speed = std::chrono::milliseconds(400);
        }

        if (click < (last_click_time + double_click_speed)
            && (cx >= (last_click_x - offsetX) && cx <= (last_click_x + offsetX))
            &&
            (cy >= (last_click_y - offsetY) && cy <= (last_click_y + offsetY))) {
            last_click_time = std::chrono::time_point<std::chrono::steady_clock>::min();
            double_tap = true;
            __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "TOUCH DBLCLK!!!!");
        } else {
            last_click_time = click;
            last_click_x = cx;
            last_click_y = cy;
        }

        ev.type = ev.MYOSD_FINGER_DOWN;
        ev.data.pointer_data.double_action = double_tap;
        myosd_pushEvent(ev);
        last_cx = cx;
        last_cy = cy;

        __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "TOUCH PULSO BT1!");

    } else if (touchAction == com_seleuco_mame4droid_Emulator_FINGER_UP)
    {
        myosd_inputevent ev;
        ev.data.pointer_data.x = cx;
        ev.data.pointer_data.y = cy;

        ev.type = ev.MYOSD_FINGER_UP;
        ev.data.pointer_data.double_action = double_tap;
        double_tap = false;
        myosd_pushEvent(ev);
    }

    return 1;
}

static void droid_dump_video(void) {

    //__android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "droid_dump_video");

    if (dumpVideo_callback != nullptr)
        dumpVideo_callback();

}

static void droid_set_video_mode(int width, int height, int vis_width, int vis_height) {

    __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "droid_set_video_mode: %d %d %d %d", width, height, vis_width, vis_height);

    if (width == 0)width = 1;
    if (height == 0)height = 1;

    myosd_droid_video_width = width;
    myosd_droid_video_height = height;

    if (changeVideo_callback != nullptr)
        changeVideo_callback(myosd_droid_video_width,
                             myosd_droid_video_height,
                             vis_width, vis_height
                             );

    droid_dump_video();
}


static void droid_init_input(){
    memset(keyboard,0, sizeof(keyboard));

    memset(mouse_x,0, sizeof(mouse_x));
    memset(mouse_y,0, sizeof(mouse_y));
    memset(mouse_status,0, sizeof(mouse_status));

    memset(lightgun_x,0, sizeof(lightgun_x));
    memset(lightgun_y,0, sizeof(lightgun_y));

    memset(joy_analog_x,0, sizeof(joy_analog_x));
    memset(joy_analog_y,0, sizeof(joy_analog_y));

    memset(joy_status,0, sizeof(joy_status));//si reseteo se inicia la rom ??

    cur_x_mouse = 0;
    cur_y_mouse = 0;
}

static void droid_map_keyboard(){

    memset(android_to_mame_key, -1, sizeof(android_to_mame_key));

    for(int i = 0; i<= AKEYCODE_9 - AKEYCODE_0;i++ )
        android_to_mame_key[AKEYCODE_0 + i] = MYOSD_KEY_0 + i;

    for(int i = 0; i<= AKEYCODE_Z - AKEYCODE_A;i++ )
        android_to_mame_key[AKEYCODE_A + i] = MYOSD_KEY_A + i;

    for(int i = 0; i<= AKEYCODE_F12 - AKEYCODE_F1;i++ )
        android_to_mame_key[AKEYCODE_F1 + i] = MYOSD_KEY_F1 + i;

    android_to_mame_key[AKEYCODE_ENTER] = MYOSD_KEY_ENTER;
    android_to_mame_key[AKEYCODE_ESCAPE] = MYOSD_KEY_ESC;
    android_to_mame_key[AKEYCODE_DEL] = MYOSD_KEY_BACKSPACE;
    android_to_mame_key[AKEYCODE_TAB] = MYOSD_KEY_TAB;
    android_to_mame_key[AKEYCODE_SPACE] = MYOSD_KEY_SPACE;
    android_to_mame_key[AKEYCODE_MINUS] = MYOSD_KEY_MINUS;
    android_to_mame_key[AKEYCODE_EQUALS] = MYOSD_KEY_EQUALS;
    android_to_mame_key[AKEYCODE_LEFT_BRACKET] = MYOSD_KEY_OPENBRACE;
    android_to_mame_key[AKEYCODE_RIGHT_BRACKET] = MYOSD_KEY_CLOSEBRACE;
    android_to_mame_key[AKEYCODE_BACKSLASH] = MYOSD_KEY_BACKSLASH;
    //android_to_mame_key[????] = MYOSD_KEY_BACKSLASH2;   // TODO: check
    android_to_mame_key[AKEYCODE_SEMICOLON] = MYOSD_KEY_COLON;
    android_to_mame_key[AKEYCODE_APOSTROPHE] = MYOSD_KEY_QUOTE;
    android_to_mame_key[AKEYCODE_GRAVE] = MYOSD_KEY_TILDE;
    android_to_mame_key[AKEYCODE_COMMA] = MYOSD_KEY_COMMA;
    android_to_mame_key[AKEYCODE_PERIOD] = MYOSD_KEY_STOP; //full stop or period key
    android_to_mame_key[AKEYCODE_SLASH] = MYOSD_KEY_SLASH;
    android_to_mame_key[AKEYCODE_CAPS_LOCK] = MYOSD_KEY_CAPSLOCK;
    //android_to_mame_key[?????] = MYOSD_KEY_BACKSLASH2;   // TODO: check

    /* special keys */
    android_to_mame_key[AKEYCODE_SYSRQ] = MYOSD_KEY_PRTSCR;
    android_to_mame_key[AKEYCODE_SCROLL_LOCK] = MYOSD_KEY_SCRLOCK;
    android_to_mame_key[AKEYCODE_BREAK] = MYOSD_KEY_PAUSE;
    android_to_mame_key[AKEYCODE_INSERT] = MYOSD_KEY_INSERT;
    android_to_mame_key[AKEYCODE_MOVE_HOME] = MYOSD_KEY_HOME;
    android_to_mame_key[AKEYCODE_PAGE_UP] = MYOSD_KEY_PGUP;
    android_to_mame_key[AKEYCODE_FORWARD_DEL] = MYOSD_KEY_DEL;
    android_to_mame_key[AKEYCODE_MOVE_END] = MYOSD_KEY_END;
    android_to_mame_key[AKEYCODE_PAGE_DOWN] = MYOSD_KEY_PGDN;
    android_to_mame_key[AKEYCODE_DPAD_RIGHT] = MYOSD_KEY_RIGHT;
    android_to_mame_key[AKEYCODE_DPAD_LEFT] = MYOSD_KEY_LEFT;
    android_to_mame_key[AKEYCODE_DPAD_DOWN] = MYOSD_KEY_DOWN;
    android_to_mame_key[AKEYCODE_DPAD_UP] = MYOSD_KEY_UP;

    /* modifier keys */
    android_to_mame_key[AKEYCODE_CTRL_LEFT] = MYOSD_KEY_LCONTROL;
    android_to_mame_key[AKEYCODE_SHIFT_LEFT] = MYOSD_KEY_LSHIFT;
    android_to_mame_key[AKEYCODE_ALT_LEFT] = MYOSD_KEY_LALT;
    android_to_mame_key[AKEYCODE_CTRL_RIGHT] = MYOSD_KEY_RCONTROL;
    android_to_mame_key[AKEYCODE_SHIFT_RIGHT ] = MYOSD_KEY_RSHIFT;
    android_to_mame_key[AKEYCODE_ALT_RIGHT] = MYOSD_KEY_RALT;

    /* command keys */
    android_to_mame_key[AKEYCODE_META_LEFT] = MYOSD_KEY_LCMD;
    android_to_mame_key[AKEYCODE_META_RIGHT] = MYOSD_KEY_RCMD;

    /* Keypad (numpad) keys */
    android_to_mame_key[AKEYCODE_NUM_LOCK] = MYOSD_KEY_NUMLOCK;
    android_to_mame_key[AKEYCODE_NUMPAD_DIVIDE] = MYOSD_KEY_SLASH;
    android_to_mame_key[AKEYCODE_NUMPAD_MULTIPLY] = MYOSD_KEY_ASTERISK;
    android_to_mame_key[AKEYCODE_NUMPAD_SUBTRACT] = MYOSD_KEY_MINUS_PAD;
    android_to_mame_key[AKEYCODE_NUMPAD_ADD] = MYOSD_KEY_PLUS_PAD;
    android_to_mame_key[AKEYCODE_NUMPAD_ENTER] = MYOSD_KEY_ENTER_PAD;
    android_to_mame_key[AKEYCODE_NUMPAD_DOT] = MYOSD_KEY_STOP;
    android_to_mame_key[AKEYCODE_NUMPAD_EQUALS] = MYOSD_KEY_EQUALS;

    for(int i = 0; i<= AKEYCODE_NUMPAD_9 - AKEYCODE_NUMPAD_0;i++ )
        android_to_mame_key[AKEYCODE_NUMPAD_0 + i] =  MYOSD_KEY_0_PAD + i;

}

static void droid_init(void) {
    if (!lib_inited) {

        __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "init");

        int reswidth = 640, resheight = 480;
        int reswidth_osd = 640, resheight_osd = 480;

        switch (myosd_droid_resolution)
        {
            case 0:{reswidth = 0;resheight = 0;break;}//400x300 (4/3)
            case 1:{reswidth = 640;resheight = 480;break;}//640x480 (4/3)
            case 2:{reswidth = 800;resheight = 600;break;}//800x600 (4/3)
            case 3:{reswidth = 1024;resheight = 768;break;}//1024x768 (4/3)
            case 4:{reswidth = 1280;resheight = 720;break;}//1280x720 (16/9)
            case 5:{reswidth = 1440;resheight = 1080;break;}//1440x1080 (4/3)
            case 6:{reswidth = 1920;resheight = 1080;break;}//1920x1080 (16/9)
            case 7:{reswidth = (myosd_droid_res_height_native/2) * 4/3.0f ;resheight = myosd_droid_res_height_native/2;break;}//fullscreen/2 (4/3)
            case 8:{reswidth = myosd_droid_res_width_native/2;resheight = myosd_droid_res_height_native/2;break;}//fullscreen/2
            case 9:{reswidth = myosd_droid_res_height_native * 4/3.0f ;resheight = myosd_droid_res_height_native;break;}//fullscreen (4/3)
            case 10:{reswidth = myosd_droid_res_width_native;resheight = myosd_droid_res_height_native;break;}//fullscreen
        }

        switch (myosd_droid_resolution_osd)
        {
            case 0:{reswidth_osd = 400;resheight_osd = 300;break;}
           // case 11:{reswidth_osd = 450;resheight_osd = 300;break;}
            case 11:{reswidth_osd = 500;resheight_osd = 333;break;}
            //case 11:{reswidth_osd = 510;resheight_osd = 300;break;}
            case 1:{reswidth_osd = 640;resheight_osd = 480;break;}//640x480 (4/3)
            case 2:{reswidth_osd = 800;resheight_osd = 600;break;}//800x600 (4/3)
            case 3:{reswidth_osd = 1024;resheight_osd = 768;break;}//1024x768 (4/3)
            case 4:{reswidth_osd = 1280;resheight_osd = 720;break;}//1280x720 (16/9)
            case 5:{reswidth_osd = 1440;resheight_osd = 1080;break;}//1440x1080 (4/3)
            case 6:{reswidth_osd = 1920;resheight_osd = 1080;break;}//1920x1080 (16/9)
            case 7:{reswidth_osd = (myosd_droid_res_height_native/2) * 4/3.0f ;resheight_osd = myosd_droid_res_height_native/2;break;}//fullscreen/2 (4/3)
            case 8:{reswidth_osd = myosd_droid_res_width_native/2;resheight_osd = myosd_droid_res_height_native/2;break;}//fullscreen/2
            case 9:{reswidth_osd = myosd_droid_res_height_native * 4/3.0f ;resheight_osd = myosd_droid_res_height_native;break;}//fullscreen (4/3)
            case 10:{reswidth_osd = myosd_droid_res_width_native;resheight_osd = myosd_droid_res_height_native;break;}//fullscreen
        }

        myosd_droid_res_width = reswidth;
        myosd_droid_res_height = resheight;
        myosd_droid_res_width_osd = reswidth_osd;
        myosd_droid_res_height_osd = resheight_osd;

        droid_set_video_mode(myosd_droid_res_width_osd, myosd_droid_res_height_osd,
                             myosd_droid_res_width_osd, myosd_droid_res_height_osd);

        droid_init_input();

        droid_map_keyboard();

        lib_inited = 1;
    }
}

int myosd_safOpenFile(const char *pathName,const char *mode) {
    if (safOpenFile_callback != nullptr) {
        __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "myosd_safOpenFile %s %s",pathName, mode);
        return safOpenFile_callback(pathName, mode);
    }
    return -1;
}

int *myosd_safReadDir(const char *dirName, int reload) {
    if (safReadDir_callback != nullptr) {
        __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "myosd_safReadDir %s",dirName);
        int res = safReadDir_callback((char *) dirName, reload);
        if(res!=0) {
            int *value= (int*)malloc(sizeof (int));
            *value = res;
            return value;
        }
    }
    return nullptr;
}

myosd_saf_dirent *myosd_safGetNextDirEntry(int *id) {
    myosd_saf_dirent *entry = nullptr;
    if (safGetNextDirEntry_callback != nullptr && id!= nullptr) {
        char **s = safGetNextDirEntry_callback(*id);
        if(s!= nullptr) {
            entry = new myosd_saf_dirent;
            entry->name = std::string(s[0]); free(s[0]);
            entry->size = atol(s[1]);free(s[1]);
            entry->modified = atol(s[2]);free(s[2]);
            entry->isDir = s[3][0] == 'D';free(s[3]);
            free(s);
        }
    }
    return entry;
}

void myosd_safCloseDir(int *id) {
    if (safCloseDir_callback != nullptr && id!= nullptr) {
        __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "myosd_safCloseDir %d",*id);
        safCloseDir_callback(*id);
        free(id);
    }
}

//myosd core callbacks

static void droid_output_cb(int channel, const char *text) {
    //TODO: capture any error/warning output for later use.
    if (channel == MYOSD_OUTPUT_ERROR) {
        __android_log_print(ANDROID_LOG_ERROR, "libMAME4droid.so", "%s", text);
    } else if (channel == MYOSD_OUTPUT_WARNING) {
        __android_log_print(ANDROID_LOG_WARN, "libMAME4droid.so", "%s", text);
    } else if (channel == MYOSD_OUTPUT_INFO) {
        __android_log_print(ANDROID_LOG_INFO, "libMAME4droid.so", "%s", text);
    }
}

static void droid_video_change_cb(int width, int height,int vis_width, int vis_height) {
    droid_set_video_mode(width, height,vis_width,vis_height);
}

static void droid_video_draw_cb(int skip_redraw, int in_game, int in_menu, int running) {

    myosd_set(MYOSD_FPS, myosd_droid_show_fps);
    myosd_set(MYOSD_ZOOM_TO_WINDOW, myosd_droid_zoom_to_window);

    if(myosd_droid_init_game && running) {
        if (myosd_speed_hacks && in_game) {
            myosd_speed_hack();
        }
        myosd_droid_init_game = 0;
    }

    myosd_droid_inGame = in_game;
    myosd_droid_inMenu = in_menu;
    myosd_droid_running = running;

    //__android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "inGame %d inMenu %d running %d",in_game,in_menu,running);

    if(!skip_redraw)
       droid_dump_video();

    droid_myosd_check_pause();
}

static void droid_video_exit_cb() {

}

static void droid_input_init_cb(myosd_input_state *input, size_t state_size) {

    //droid_init_input();

    cur_x_mouse = 0;
    cur_y_mouse = 0;

    myosd_droid_num_buttons   = input->num_buttons;
    myosd_droid_num_ways      = input->num_ways;
    myosd_droid_light_gun     = input->num_lightgun != 0;
    myosd_droid_mouse     = input->num_mouse != 0;
    myosd_droid_init_game = 1;


    /*
    myosd_droid_num_players   = myosd->num_players;
    myosd_droid_num_coins     = myosd->num_coins;
    myosd_droid_num_inputs    = myosd->num_inputs;
    myosd_droid_has_keyboard  = myosd->num_keyboard != 0;
    */

    if (initInput_callback != nullptr)
        initInput_callback();

}

static void droid_input_poll_cb(bool relative_reset,
                                myosd_input_state *input, size_t state_size) {

    for(int i=0; i<MYOSD_NUM_JOY; i++) {
        input->joy_status[i] = joy_status[i];
        input->joy_analog[i][MYOSD_AXIS_RX] = joy_analog_x[i];
        input->joy_analog[i][MYOSD_AXIS_RY] = joy_analog_y[i];
        input->joy_analog[i][MYOSD_AXIS_RZ] = joy_analog_trigger_x[i];
        input->joy_analog[i][MYOSD_AXIS_LZ] = joy_analog_trigger_y[i];
    }

    for(int i=0; i<MYOSD_NUM_GUN; i++) {
        input->lightgun_x[i] = lightgun_x[i];
        input->lightgun_y[i] = lightgun_y[i];
    }

    if(myosd_droid_mouse_enable) {
        if(relative_reset) {
            for (int i = 0; i < MYOSD_NUM_MICE; i++) {
                input->mouse_status[i] = mouse_status[i];

                input->mouse_x[i] = mouse_x[i] * 512;
                input->mouse_y[i] = mouse_y[i] * 512;

                pthread_mutex_lock(&mouse_mutex);

                mouse_x[i] = 0;
                mouse_y[i] = 0;

                pthread_mutex_unlock(&mouse_mutex);
            }
        }
    }

    //if(myosd_droid_keyboard_enable) { always needs key support as exit is handled as virtual key
        for (int i = 0; i < MYOSD_NUM_KEYS; i++) {
            input->keyboard[i] = keyboard[i];
        }
    //}

    if(myosd_droid_do_pause){
        myosd_pause(true);
        myosd_droid_do_pause = 0;
    }

    if(myosd_droid_do_resume){
        myosd_pause(false);
        myosd_droid_do_resume = 0;
    }
}

void droid_sound_init_cb(int rate, int stereo){
    if (soundInit == 0 && myosd_droid_sound_value != -1) {

        __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "openSound rate:%d stereo:%d",rate,stereo);

        if (sound_engine == 1) {
            __android_log_print(ANDROID_LOG_DEBUG, "SOUND", "Open audioTrack");
            if (openSound_callback != nullptr)
                openSound_callback(rate, stereo);
        } else {
            __android_log_print(ANDROID_LOG_DEBUG, "SOUND", "Open openSL %d %d", myosd_droid_sound_value, myosd_droid_sound_frames);
            opensl_snd_ptr  = opensl_open(myosd_droid_sound_value, 2, myosd_droid_sound_frames);
        }

        soundInit = 1;
    }
}
void droid_sound_play_cb(void *buff, int len){
    //__android_log_print(ANDROID_LOG_DEBUG, "PIS", "BUF %d",len);
    if(sound_engine==1)
    {
        if(dumpSound_callback!=nullptr)
            dumpSound_callback(buff,len);
    }
    else
    {
        if(opensl_snd_ptr !=nullptr)
            opensl_write(opensl_snd_ptr ,(short *)buff, len / 2);
    }
}
void droid_sound_exit_cb(){
    if (soundInit == 1) {

        __android_log_print(ANDROID_LOG_DEBUG, "MAME4droid.so", "closeSound");

        if (sound_engine == 1) {
            if (closeSound_callback != nullptr)
                closeSound_callback();
        } else {
            if (opensl_snd_ptr  != nullptr)
                opensl_close(opensl_snd_ptr );
        }

        soundInit = 0;
    }
}

static void split_in_args(std::vector <std::string> &qargs, std::string command) {
    int len = command.length();
    bool sqot = false;
    int arglen;
    for (int i = 0; i < len; i++) {
        int start = i;
        if (command[i] == '\'') {
            sqot = true;
        }
        if (sqot) {
            i++;
            start++;
            while (i < len && command[i] != '\'')
                i++;
            if (i < len)
                sqot = false;
            arglen = i - start;
            i++;
        } else {
            while (i < len && command[i] != ' ')
                i++;
            arglen = i - start;
        }
        qargs.push_back(command.substr(start, arglen));
    }
}

//main entry point to MAME
int myosd_droid_main(int argc, char **argv) {

    __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "*********** ANDROID MAIN ********");

    droid_init();

    myosd_callbacks callbacks = {
            .output_text  = droid_output_cb,
            .video_change = droid_video_change_cb,
            .video_draw   = droid_video_draw_cb,
            .video_exit   = droid_video_exit_cb,
            .input_init   = droid_input_init_cb,
            .input_poll   = droid_input_poll_cb,
            .sound_init   = droid_sound_init_cb,
            .sound_play   = droid_sound_play_cb,
            .sound_exit   = droid_sound_exit_cb,
/*
            .game_list = m4i_game_list,
            .game_init = m4i_game_start,
            .game_exit = m4i_game_stop,
*/
    };

    myosd_set(MYOSD_DISPLAY_WIDTH, myosd_droid_res_width);
    myosd_set(MYOSD_DISPLAY_HEIGHT, myosd_droid_res_height);
    myosd_set(MYOSD_DISPLAY_WIDTH_OSD, myosd_droid_res_width_osd);
    myosd_set(MYOSD_DISPLAY_HEIGHT_OSD, myosd_droid_res_height_osd);

    static const char *args[255];
    int n = 0;
    args[n] = "mame4x";
    n++;

    if(!myosd_droid_rom_name.empty())
    {
        args[n]= myosd_droid_rom_name.c_str(); n++;
    }

    if(myosd_droid_simple_ui) {
        args[n] = "-ui";
        n++;
        args[n] = "simple";
        n++;
    }

    if(myosd_droid_warn_on_exit) {
        args[n] = "-confirm_quit";n++;
    }
    else
    {
        args[n] = "-noconfirm_quit";n++;
    }

    if(!myosd_droid_using_mameini && myosd_droid_using_saf && !myosd_droid_safpath.empty())
    {
        static std::string rp = myosd_droid_safpath+std::string(";./roms");
        args[n]= "-rompath"; n++;args[n]=rp.c_str(); n++;

        static std::string ap = myosd_droid_safpath+std::string("/artwork;./artwork");
        args[n]= "-artpath"; n++;args[n]=ap.c_str(); n++;

        static std::string sp = myosd_droid_safpath+std::string("/samples;./samples");
        args[n]= "-samplepath"; n++;args[n]=sp.c_str(); n++;

        static std::string swp = myosd_droid_safpath+std::string("/software;./software");
        //static std::string swp = myosd_droid_safpath+std::string("/software;");
        args[n]= "-swpath"; n++;args[n]=swp.c_str(); n++;

        if(myosd_droid_savestatesinrompath)
        {
            static std::string sta = myosd_droid_safpath+std::string("/sta");
            args[n]= "-state_directory"; n++;args[n]=sta.c_str(); n++;
        }
    }

    if(!myosd_droid_overlay_effect.empty()) {
        args[n] = "-effect";
        n++;
        args[n] =  myosd_droid_overlay_effect.c_str();
        n++;
    }


    if(myosd_num_processors!=-1) {
        __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "Num processors: %d",myosd_num_processors);
        args[n] = "-numprocessors";
        n++;
        args[n] = std::to_string(myosd_num_processors).c_str();
        n++;
    }

    args[n] = "-ui_active";
    n++;

    args[n] = "-natural";
    n++;

    args[n] = "-nocoin_lockout";
    n++;

    if(myosd_droid_cheats) {
        args[n] = "-cheat";
        n++;
    }

    if(myosd_droid_skip_gameinfo) {
        args[n] = "-skip_gameinfo";
        n++;
    }

    if(myosd_droid_disable_drc) {
        __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "NO DRC");
        args[n] = "-nodrc";
        n++;
    }

    if(!myosd_droid_disable_drc && myosd_droid_enable_drc_use_c) {
        __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "DRC but C backend");
        args[n] = "-drc";
        n++;
        args[n] = "-drc_use_c";
        n++;
    }

    if(!myosd_droid_disable_drc && !myosd_droid_enable_drc_use_c) {
        __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "DRC");
        args[n] = "-drc";
        n++;
        args[n] = "-nodrc_use_c";
        n++;
    }

    if(myosd_droid_vector_beam2x) {

        //-beam_width_min 0.75 -beam_width_max 2.5 -beam_intensity_weight 1.75
        args[n] = "-beam_width_min";n++;
        args[n] = "2.0";n++;
        args[n] = "-beam_width_max";n++;
        args[n] = "3.9";n++;
        args[n] = "-beam_intensity_weight";n++;
        args[n] = "0.75";n++;
    }

    if(myosd_droid_vector_flicker) {
        args[n] = "-flicker";n++;
        args[n] = "0.4";n++;
    }

    if(myosd_droid_sound_value==-1)
    {
        args[n] = "-sound";n++;
        args[n] = "none";n++;
    }
    else
    {
        static std::string value = std::to_string(myosd_droid_sound_value);
        args[n] = "-samplerate";n++;
        args[n] = value.c_str();n++;
    }

    if(myosd_droid_auto_frameskip)
    {
        args[n] = "-afs";n++;
    }

    if(myosd_droid_no_dzsat)
    {
        args[n] = "-jdz";n++;
        args[n] = "0.0";n++;
        args[n] = "-jsat";n++;
        args[n] = "1.0";n++;
    }

    if(myosd_droid_resolution==0 || myosd_droid_resolution_osd==0)
    {
        args[n] = "-uifont";n++;
        args[n] = "uismall.bdf";n++;
    }

    if(myosd_plugin_autofire !=0 || myosd_plugin_hiscore !=0 || myosd_plugin_inputmacro !=0)
    {
        args[n] = "-plugin";n++;
        static std::string plugin_list = "";

        if(myosd_plugin_autofire!=0)
            plugin_list+="autofire,";
        if(myosd_plugin_hiscore!=0)
            plugin_list+="hiscore,";
        if(myosd_plugin_inputmacro!=0)
            plugin_list+="inputmacro,";

        args[n] =  plugin_list.c_str();n++;
    }

    if(0)
    {
        args[n] =  "-v";n++;
    }

    if(!myosd_droid_cli_params.empty())
    {
        static std::vector <std::string> tokens;

        split_in_args(tokens,myosd_droid_cli_params);

        for(int i = 0; i < tokens.size(); i++) {
            __android_log_print(ANDROID_LOG_DEBUG, "libMAME4droid.so", "cli param %s",
                                tokens[i].c_str());
            args[n] = tokens[i].c_str();
            n++;
        }
    }

    myosd_main(n, (char**)args, &callbacks, sizeof(callbacks));

    lib_inited = 0;

    return 0;
}



