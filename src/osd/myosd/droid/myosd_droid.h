// license:BSD-3-Clause
//============================================================
//
//  myosd_droid.h - Header of osd droid stuff
//
//  MAME4DROID by David Valdeita (Seleuco)
//
//============================================================

#ifndef __MYOSD_DROID_H__
#define __MYOSD_DROID_H__

#if defined(__cplusplus)
extern "C" {
#endif

void myosd_droid_setVideoCallbacks(
        void (*dump_video_java)(void),
        void (*change_video_java)(int, int, int, int));
void myosd_droid_setAudioCallbacks(
        void (*open_sound_java)(int, int),
        void (*dump_sound_java)(void *, int),
        void (*close_sound_java)(void));
void myosd_droid_setInputCallbacks(
        void (*init_input)(void));
void myosd_droid_setSAFCallbacks(
        int (*safOpenFile_java)(const char *,const char *),
        int (*safReadDir_java)(const char *, int reload),
        char **(*safGetNextDirEntry_java)(int),
        void (*safCloseDir_java)(int),
        int (*safDeleteFile_java)(const char *));

void myosd_droid_initMyOSD(const char *path, int nativeWidth, int nativeHeight);
void myosd_droid_setNativeSize(int nativeWidth, int nativeHeight);
void myosd_droid_setMyValue(int key, int i, int value);
int myosd_droid_getMyValue(int key, int i);
void myosd_droid_setMyValueStr(int key, int i, const char *value);
char *myosd_droid_getMyValueStr(int key, int i);
void myosd_droid_setDigitalData(int i, unsigned long pad_status);
void myosd_droid_setAnalogData(int t, int i, float v1, float v2);
int myosd_droid_setKeyData(int keyCode,int keyAction,char keyChar);
int myosd_droid_setMouseData(int i, int mouseAction,int button,float x, float y);
int myosd_droid_setTouchData(int i, int touchAction,float x, float y);

int myosd_droid_main  (int argc, char **argv);

#if defined(__cplusplus)
}
#endif

// Boot-time UI rows policy (20 low-res OSD / 25 normal, user picks kept);
// consumed by mame_ui_manager::load_ui_options(). 0 = keep current value
int myosd_droid_adjust_ui_font_rows(int current);

// Netplay specific helpers (C++ linkage; see myosd_droid.cpp's Netplay
// BRIDGE section).  Callers outside this file currently redeclare these
// locally rather than including this header.
int myosd_droid_netplay_get_inMenu();              // whether the MAME menu is up
void myosd_droid_netplay_set_exitPause(int val);   // unpause on netplay resume
int myosd_droid_netplay_get_ext_status();          // reserved input-vector extension field
int myosd_droid_netplay_restart_pending(void);     // 1 while a netplay-forced reload is pending/in flight
int myosd_droid_netplay_plugins_allowed(void);     // opt-in: run Lua plugins during netplay (may desync)

// Per-frame local input read-back, called from netplay.cpp
unsigned long myosd_droid_netplay_joystick_read(int i);
float myosd_droid_netplay_joystick_read_analog(int i, char axis);

unsigned long myosd_droid_netplay_mouse_read(int i);
float myosd_droid_netplay_mouse_read_analog(int i, char axis);
float myosd_droid_netplay_lightgun_read_analog(int i, char axis);

#endif
