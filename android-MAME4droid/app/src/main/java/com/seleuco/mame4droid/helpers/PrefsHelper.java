/*
 * This file is part of MAME4droid.
 *
 * Copyright (C) 2024 David Valdeita (Seleuco)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses>.
 *
 * Linking MAME4droid statically or dynamically with other modules is
 * making a combined work based on MAME4droid. Thus, the terms and
 * conditions of the GNU General Public License cover the whole
 * combination.
 *
 * In addition, as a special exception, the copyright holders of MAME4droid
 * give you permission to combine MAME4droid with free software programs
 * or libraries that are released under the GNU LGPL and with code included
 * in the standard release of MAME under the MAME License (or modified
 * versions of such code, with unchanged license). You may copy and
 * distribute such a system following the terms of the GNU GPL for MAME4droid
 * and the licenses of the other code concerned, provided that you include
 * the source code of that other code when and as the GNU GPL requires
 * distribution of source code.
 *
 * Note that people who make modified versions of MAME4idroid are not
 * obligated to grant this special exception for their modified versions; it
 * is their choice whether to do so. The GNU General Public License
 * gives permission to release a modified version without this exception;
 * this exception also makes it possible to release a modified version
 * which carries forward this exception.
 *
 * MAME4droid is dual-licensed: Alternatively, you can license MAME4droid
 * under a MAME license, as set out in http://mamedev.org/
 */

package com.seleuco.mame4droid.helpers;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.SharedPreferences.OnSharedPreferenceChangeListener;
import android.preference.PreferenceManager;

import com.seleuco.mame4droid.Emulator;
import com.seleuco.mame4droid.MAME4droid;
import com.seleuco.mame4droid.input.GameController;

public class PrefsHelper implements OnSharedPreferenceChangeListener {
	final static public String PREF_ROMsDIR = "PREF_ROMsDIR_2";
	final static public String PREF_SAF_URI = "PREF_SAF_URI";
	final static public String PREF_INSTALLATION_DIR = "PREF_INSTALLATION_DIR";
	final static public String PREF_OLD_INSTALLATION_DIR = "PREF_OLD_INSTALLATION_DIR";

	final static public String PREF_GLOBAL_VIDEO_RENDER_MODE = "PREF_GLOBAL_VIDEO_RENDER_MODE";

	final static public String PREF_EMU_RESOLUTION = "PREF_EMU_RESOLUTION_3";
	final static public String PREF_EMU_RESOLUTION_OSD = "PREF_EMU_RESOLUTION_OSD_3";
	final static public String PREF_EMU_SOUND = "PREF_EMU_SOUND";
	final static public String PREF_EMU_SHOW_FPS = "PREF_EMU_SHOW_FPS";
	final static public String PREF_ZOOM_TO_WINDOW = "PREF_ZOOM_TO_WINDOW";
	final static public String PREF_EMU_AUTO_FRAMESKIP = "PREF_EMU_AUTO_FRAMESKIP";
	final static public String CHEATS = "CHEATS";
	final static public String SKIP_GAMEINFO = "SKIP_GAMEINFO";
	final static public String PREF_EMU_DISABLE_DRC = "PREF_EMU_DISABLE_DRC_4";
	final static public String PREF_EMU_DRC_USR_C = "PREF_EMU_DRC_USE_C_4";
	final static public String PREF_EMU_NUM_PROCESSORS = "PREF_EMU_NUM_PROCESSORS";
	final static public String PREF_MAMEINI = "PREF_MAMEINI";
	final static public String PREF_SPEED_HACKS = "PREF_SPEED_HACKS";
	final static public String PREF_HISCORE = "PREF_HISCORE";
	final static public String PREF_INPUTMACRO = "PREF_INPUTMACRO";
	final static public String PREF_AUTOFIRE = "PREF_AUTOFIRE";

	final static public String PREF_GLOBAL_AUTOSAVE = "PREF_GLOBAL_AUTOSAVE";
	final static public String PREF_GLOBAL_DEBUG = "PREF_GLOBAL_DEBUG";
	final static public String PREF_GLOBAL_WARN_ON_EXIT = "PREF_GLOBAL_WARN_ON_EXIT";

	final static public String PREF_SHADERS = "shaders";
	final static public String PREF_SHADERS_ENABLED = "PREF_SHADERS_ENABLED";
	final static public String PREF_SHADER_EFFECT = "PREF_SHADER_EFFECT_2";

	final static public String PREF_SCRAPE_ENABLED = "PREF_SCRAPE_ENABLED";
	final static public String PREF_SCRAPE_ICONS = "PREF_SCRAPE_ICONS";
	final static public String PREF_SCRAPE_SNAPSHOTS = "PREF_SCRAPE_SNAPSHOTS";
	final static public String PREF_SCRAPE_ALL = "PREF_SCRAPE_ALL";
	final static public String PREF_SCRAPE_RESIZE = "PREF_SCRAPE_RESIZE";
	final static public String PREF_BITMAP_FILTERING = "PREF_BITMAP_FILTERING";

	final static public String PREF_PORTRAIT_SCALING_MODE = "PREF_PORTRAIT_SCALING_MODE";
	final static public String PREF_PORTRAIT_TOUCH_CONTROLLER = "PREF_PORTRAIT_TOUCH_CONTROLLER";
	final static public String PREF_PORTRAIT_FULLSCREEN = "PREF_PORTRAIT_FULLSCREEN";

	final static public String PREF_LANDSCAPE_SCALING_MODE = "PREF_LANDSCAPE_SCALING_MODE";
	final static public String PREF_LANDSCAPE_TOUCH_CONTROLLER = "PREF_LANDSCAPE_TOUCH_CONTROLLER";
	final static public String PREF_LANDSCAPE_CONTROLLER_TYPE = "PREF_LANDSCAPE_CONTROLLER_TYPE";

	final static public String PREF_DEFINED_KEYS = "PREF_DEFINED_KEYS";

	final static public String PREF_DEFINED_CONTROL_LAYOUT = "PREF_DEFINED_CONTROL_LAYOUT";
	final static public String PREF_DEFINED_CONTROL_LAYOUT_P = "PREF_DEFINED_CONTROL_LAYOUT_P";

	final static public String PREF_DISABLE_RIGHT_STICK = "PREF_DISABLE_RIGHT_STICK";
	final static public String PREF_CONTROLLER_DISABLE_BUTTONS_IN_FRONTEND = "PREF_CONTROLLER_DISABLE_BUTTONS_IN_FRONTEND";
	final static public String PREF_CONTROLLER_DISABLE_BUTTONS_IN_GAME = "PREF_CONTROLLER_DISABLE_BUTTONS_IN_GAME";

	final static public String PREF_ANIMATED_INPUT = "PREF_ANIMATED_INPUT";
	final static public String PREF_TOUCH_LIGHTGUN = "PREF_TOUCH_LIGHTGUN";
	final static public String PREF_TOUCH_LIGHTGUN_FORCE = "PREF_TOUCH_LIGHTGUN_FORCE";
	final static public String PREF_TOUCH_DZ = "PREF_TOUCH_DZ";
	final static public String PREF_CONTROLLER_TYPE = "PREF_CONTROLLER_TYPE";
	final static public String PREF_STICK_TYPE = "PREF_STICK_TYPE";
	final static public String PREF_NUMBUTTONS = "PREF_NUMBUTTONS";
	final static public String PREF_CONTROLLER_AUTODETECT = "PREF_CONTROLLER_AUTODETECT";
	final static public String PREF_ANALOG_DZ = "PREF_ANALOG_DZ";
	final static public String PREF_GAMEPAD_DZ = "PREF_GAMEPAD_DZ";
	final static public String PREF_VIBRATE = "PREF_VIBRATE";
	final static public String PREF_MOUSE = "PREF_MOUSE";
	final static public String PREF_TOUCH_MOUSE = "PREF_TOUCH_MOUSE";
	final static public String PREF_TOUCH_UI = "PREF_TOUCH_UI";
	final static public String PREF_TOUCH_GAME_MOUSE = "PREF_TOUCH_GAME_MOUSE";
	final static public String PREF_TOUCH_GAME_MOUSE_FORCE = "PREF_TOUCH_GAME_MOUSE_FORCE";
	final static public String PREF_TOUCH_GAME_MOUSE_HIDE_CONTROLLER = "PREF_TOUCH_GAME_MOUSE_HIDE_CONTROLLER";
	final static public String PREF_KEYBOARD = "PREF_KEYBOARD";
	final static public String PREF_KEYBOARD_HIDE_CONTROLLER = "PREF_KEYBOARD_HIDE_CONTROLLER";
	final static public String PREF_VIRTUAL_KEYBOARD = "PREF_VIRTUAL_KEYBOARD";
	final static public String PREF_INPUT_FAKE_ID = "PREF_INPUT_FAKE_ID";

	final static public String PREF_TILT_SENSOR = "PREF_TILT_SENSOR";
	final static public String PREF_TILT_DZ = "PREF_TILT_DZ";
	final static public String PREF_TILT_SENSITIVITY = "PREF_TILT_SENSITIVITY";
	final static public String PREF_TILT_NEUTRAL = "PREF_TILT_NEUTRAL";
	final static public String PREF_TILT_ANALOG = "PREF_TILT_ANALOG";
	final static public String PREF_TILT_TOUCH = "PREF_TILT_TOUCH";
	final static public String PREF_TILT_SWAP_YZ = "PREF_TILT_SWAP_YZ";
	final static public String PREF_TILT_INVERT_X = "PREF_TILT_INVERT_X";

	final static public String PREF_HIDE_STICK = "PREF_HIDE_STICK";
	final static public String PREF_ALWAYS_GH_BUTTONS = "PREF_ALWAYS_GH_BUTTONS";
	final static public String PREF_BUTTONS_ALPHA = "PREF_BUTTONS_ALPHA";
	final static public String PREF_BUTTONS_SIZE = "PREF_BUTTONS_SIZE";
	final static public String PREF_STICK_SIZE = "PREF_STICK_SIZE";
	final static public String PREF_MAIN_THREAD_PRIORITY = "PREF_MAIN_THREAD_PRIORITY";
	final static public String PREF_SOUND_ENGINE = "PREF_SOUND_ENGINE";

	final static public String PREF_FORCE_ALTGLPATH = "PREF_FORCE_ALTGLPATH";
	final static public String PREF_PXASP1 = "PREF_PXASP1";
	final static public String PREF_NODEADZONEANDSAT = "PREF_NODEADZONEANDSAT";
	final static public String SAVESATES_IN_ROM_PATH = "SAVESATES_IN_ROM_PATH";

	final static public String PREF_BEAM2X = "PREF_BEAM2X";
	final static public String PREF_FLICKER = "PREF_FLICKER";

	final static public String PREF_GLOBAL_NAVBAR_MODE = "PREF_GLOBAL_NAVBAR_MODE";
	final static public String PREF_GLOBAL_SCALE_BEYOND = "PREF_GLOBAL_SCALE_BEYOND";
	final static public String PREF_GLOBAL_OVERSCAN = "PREF_GLOBAL_OVERSCAN";
	final static public String PREF_GLOBAL_USE_NOTCH = "PREF_GLOBAL_USE_NOTCH";
	final static public String PREF_GLOBAL_SIMPLE_UI = "PREF_GLOBAL_SIMPLE_UI";
	final static public String PREF_OVERLAY = "PREF_OVERLAY";
	final static public String PREF_ORIENTATION = "PREF_ORIENTATION";

	final static public String PREF_MAME_DEFAULTS = "PREF_MAME_DEFAULTS";
	final static public String PREF_LIGHTGUN_LONGPRESS = "PREF_LIGHTGUN_LONGPRESS";
	final static public String PREF_BOTTOM_RELOAD = "PREF_BOTTOM_RELOAD";


	final static public int LOW = 1;
	final static public int NORMAL = 2;
	final static public int HIGHT = 2;

	final static public int PREF_DIGITAL_DPAD = 1;
	final static public int PREF_DIGITAL_STICK = 2;
	final static public int PREF_ANALOG_STICK = 3;

	final static public int PREF_INPUT_NO_DETECT_CONTROLLER = 1;
	final static public int PREF_INPUT_DETECT_CONTROLLER = 2;


	final public static int PREF_ORIGINAL = 3;
	final public static int PREF_15X = 4;
	final public static int PREF_20X = 5;
	final public static int PREF_SCALE = 1;
	final public static int PREF_STRETCH = 2;
	final public static int PREF_SCALE_INTEGER = 14;
	final public static int PREF_SCALE_INTEGER_BEYOND = 15;

	final public static String PREF_OVERLAY_NONE = "none";

	final public static int PREF_AUTOMAP_THUMBS_DISABLED_L2R2_AS_L1R2 = 1;
	final public static int PREF_AUTOMAP_THUMBS_AS_COINSTART_L2R2_AS_L1R2 = 2;
	final public static int PREF_AUTOMAP_THUMBS_AS_COINSTART_L2R2_DISABLED = 3;
	final public static int PREF_AUTOMAP_THUMBS_DISABLED_L2R2_AS_COINSTART = 4;
	final public static int PREF_AUTOMAP_L1R1_AS_COINSTART_L2R2_AS_L1R1 = 5;
	final public static int PREF_AUTOMAP_L1R1_AS_EXITMENU_L2R2_AS_L1R1 = 6;

	final public static int PREF_SNDENG_AUDIOTRACK = 1;
	final public static int PREF_SNDENG_AUDIOTRACK_HIGH = 2;
	final public static int PREF_SNDENG_OPENSL = 3;
	final public static int PREF_SNDENG_OPENSL_LOW = 4;

	final public static int PREF_NAVBAR_VISIBLE = 0;
	final public static int PREF_NAVBAR_DIMM_OR_HIDE = 1;
	final public static int PREF_NAVBAR_IMMERSIVE = 2;

	protected MAME4droid mm = null;

	public PrefsHelper(MAME4droid value) {
		mm = value;
	}

	public void onSharedPreferenceChanged(SharedPreferences sharedPreferences,
										  String key) {
	}

	public void resume() {
		Context context = mm.getApplicationContext();
		SharedPreferences prefs =
			PreferenceManager.getDefaultSharedPreferences(context);
		prefs.registerOnSharedPreferenceChangeListener(this);
	}

	public void pause() {

		Context context = mm.getApplicationContext();
		SharedPreferences prefs =
			PreferenceManager.getDefaultSharedPreferences(context);
		prefs.unregisterOnSharedPreferenceChangeListener(this);
	}

	public SharedPreferences getSharedPreferences() {
		Context context = mm.getApplicationContext();
		return PreferenceManager.getDefaultSharedPreferences(context);
	}

	public boolean isBitmapFiltering() {
		return getSharedPreferences().getBoolean(PREF_BITMAP_FILTERING, true);
	}

	public int getPortraitScaleMode() {
		return Integer.valueOf(getSharedPreferences().getString(PREF_PORTRAIT_SCALING_MODE, "1")).intValue();
	}

	public int getLandscapeScaleMode() {
		return Integer.valueOf(getSharedPreferences().getString(PREF_LANDSCAPE_SCALING_MODE, "1")).intValue();
	}

	public String getOverlayFilterValue() {
		return getSharedPreferences().getString(PREF_OVERLAY, PrefsHelper.PREF_OVERLAY_NONE);
	}

	public int getOrientationMode() {
		return Integer.valueOf(getSharedPreferences().getString(PREF_ORIENTATION, "0")).intValue();
	}

	public boolean isPortraitTouchController() {
		return getSharedPreferences().getBoolean(PREF_PORTRAIT_TOUCH_CONTROLLER, true);
	}

	public boolean isPortraitFullscreen() {
		return getSharedPreferences().getBoolean(PREF_PORTRAIT_FULLSCREEN, false);
	}

	public boolean isLandscapeTouchController() {
		return getSharedPreferences().getBoolean(PREF_LANDSCAPE_TOUCH_CONTROLLER, true);
	}

	public String getDefinedKeys() {

		SharedPreferences p = getSharedPreferences();

		StringBuffer defaultKeys = new StringBuffer();

		for (int i = 0; i < GameController.defaultKeyMapping.length; i++)
			defaultKeys.append(GameController.defaultKeyMapping[i] + ":");

		return p.getString(PREF_DEFINED_KEYS, defaultKeys.toString());

	}

	public int getVideoRenderMode() {
		return Integer.valueOf(getSharedPreferences().getString(PREF_GLOBAL_VIDEO_RENDER_MODE, "1")).intValue();
	}

	public int getEmulatedResolution() {
		return Integer.valueOf(getSharedPreferences().getString(PREF_EMU_RESOLUTION, "1")).intValue();
	}

	public int getOSDResolution() {
		return Integer.valueOf(getSharedPreferences().getString(PREF_EMU_RESOLUTION_OSD, "9")).intValue();
	}

	public boolean isWarnOnExit() {
		return getSharedPreferences().getBoolean(PREF_GLOBAL_WARN_ON_EXIT, true);
	}

	public int getSoundValue() {
		return Integer.valueOf(getSharedPreferences().getString(PREF_EMU_SOUND, "44100")).intValue();
	}

	public boolean isFPSShowed() {
		return getSharedPreferences().getBoolean(PREF_EMU_SHOW_FPS, false);
	}

	public boolean isZoomToWindow() {
		return getSharedPreferences().getBoolean(PREF_ZOOM_TO_WINDOW, true);
	}

	public boolean isAutoFrameSkip() {
		return getSharedPreferences().getBoolean(PREF_EMU_AUTO_FRAMESKIP, false);
	}

	public boolean isCheats() {
		return getSharedPreferences().getBoolean(CHEATS, false);
	}

	public boolean isSkipGameInfo() {
		return getSharedPreferences().getBoolean(SKIP_GAMEINFO, false);
	}

	public boolean isDisabledDRC() {
		return getSharedPreferences().getBoolean(PREF_EMU_DISABLE_DRC, false);
	}

	public boolean isDRCUseC() {
		return getSharedPreferences().getBoolean(PREF_EMU_DRC_USR_C, false);
	}

	public int getNumProcessors() {
		return Integer.valueOf(getSharedPreferences().getString(PREF_EMU_NUM_PROCESSORS, "-1")).intValue();
	}

	public boolean isAutosave() {
		return getSharedPreferences().getBoolean(PREF_GLOBAL_AUTOSAVE, false);
	}

	public boolean isDebugEnabled() {
		return getSharedPreferences().getBoolean(PREF_GLOBAL_DEBUG, false);
	}

	public boolean isHideStick() {
		return getSharedPreferences().getBoolean(PREF_HIDE_STICK, false);
	}

	public boolean isAlwaysGH() {
		return getSharedPreferences().getBoolean(PREF_ALWAYS_GH_BUTTONS, false);
	}

	public boolean isDisabledRightStick() {
		return getSharedPreferences().getBoolean(PREF_DISABLE_RIGHT_STICK, false);
	}

	public boolean isDisabledAllButtonsInFronted() {
		return getSharedPreferences().getBoolean(PREF_CONTROLLER_DISABLE_BUTTONS_IN_FRONTEND, false);
	}

	public boolean isDisabledAllButtonsInGame() {
		return getSharedPreferences().getBoolean(PREF_CONTROLLER_DISABLE_BUTTONS_IN_GAME, false);
	}

	public boolean isAnimatedInput() {
		return getSharedPreferences().getBoolean(PREF_ANIMATED_INPUT, true);
	}

	public boolean isTouchDZ() {
		return getSharedPreferences().getBoolean(PREF_TOUCH_DZ, true);
	}

	public int getControllerType() {
		return Integer.valueOf(getSharedPreferences().getString(PREF_CONTROLLER_TYPE, "1")).intValue();
	}

	public boolean isTouchLightgunForced() {
		return getSharedPreferences().getBoolean(PREF_TOUCH_LIGHTGUN_FORCE, false);
	}

	public boolean isTouchLightgun() {

		if (!Emulator.isInGame())
			return false;

		boolean enabled = getSharedPreferences().getBoolean(PREF_TOUCH_LIGHTGUN, true);

		if (enabled &&

			(Emulator.getValue(Emulator.IS_LIGHTGUN) == 1 || isTouchLightgunForced())

			&& !this.isTiltSensorEnabled() && !mm.getInputHandler().getMouse().isEnabled())
			return true;

		return false;
	}

	public boolean isMouseEnabled() {
		return getSharedPreferences().getBoolean(PREF_MOUSE, true);
	}

	public boolean isTouchMouseEnabled() {
		if (mm.getInputHandler().getMouse().isEnabled())
			return false;

		return getSharedPreferences().getBoolean(PREF_TOUCH_MOUSE, true);
	}

	public boolean isTouchUI() {
		return getSharedPreferences().getBoolean(PREF_TOUCH_UI, true);
	}

	public boolean isTouchGameMouseForced() {
		return getSharedPreferences().getBoolean(PREF_TOUCH_GAME_MOUSE_FORCE, false);
	}

	public boolean isTouchGameMouseHideController() {
		return getSharedPreferences().getBoolean(PREF_TOUCH_GAME_MOUSE_HIDE_CONTROLLER, false);
	}

	public boolean isTouchGameMouse() {

		if (!isTouchMouseEnabled())
			return false;

		if (!getSharedPreferences().getBoolean(PREF_TOUCH_GAME_MOUSE, true))
			return false;

		if (!Emulator.isInGame() && Emulator.isInMenu())
			return false;

		if (mm.getInputHandler().getTiltSensor().isEnabled())
			return false;

		if (mm.getInputHandler().getGameController().isEnabled())
			return false;

		if (mm.getInputHandler().getMouse().isEnabled())
			return false;

		if (isTouchLightgun())
			return false;

		return Emulator.getValue(Emulator.IS_MOUSE) == 1 || isTouchGameMouseForced();

	}

	public boolean isKeyboardEnabled() {
		return getSharedPreferences().getBoolean(PREF_KEYBOARD, true);
	}

	public boolean isKeyboardHideController() {
		return getSharedPreferences().getBoolean(PREF_KEYBOARD_HIDE_CONTROLLER, true);
	}

	public boolean isVirtualKeyboardEnabled() {
		return getSharedPreferences().getBoolean(PREF_VIRTUAL_KEYBOARD, true);
	}

	public int getStickWays() {
		return Integer.valueOf(getSharedPreferences().getString(PREF_STICK_TYPE, "-1")).intValue();
	}

	public int getNumButtons() {
		int n = Integer.valueOf(getSharedPreferences().getString(PREF_NUMBUTTONS, "-1")).intValue();
		if (n == 33) n = 3;
		return n;
	}

	public boolean isBplusX() {
		return Integer.valueOf(getSharedPreferences().getString(PREF_NUMBUTTONS, "-1")).intValue() == 33;
	}

	public boolean isContollerAutodetect() {
		return getSharedPreferences().getBoolean(PREF_CONTROLLER_AUTODETECT, true);
	}

	public int getAnalogDZ() {
		return Integer.valueOf(getSharedPreferences().getString(PREF_ANALOG_DZ, "2")).intValue();
	}

	public int getGamepadDZ() {
		return Integer.valueOf(getSharedPreferences().getString(PREF_GAMEPAD_DZ, "3")).intValue();
	}

	public boolean isVibrate() {
		return getSharedPreferences().getBoolean(PREF_VIBRATE, false);
	}

	public String getROMsDIR() {
		return getSharedPreferences().getString(PREF_ROMsDIR, null);
	}

	public void setROMsDIR(String value) {
		//PreferenceManager.getDefaultSharedPreferences(this);
		SharedPreferences.Editor editor = getSharedPreferences().edit();
		editor.putString(PREF_ROMsDIR, value);
		editor.commit();
	}

	public String getSAF_Uri() {
		return getSharedPreferences().getString(PREF_SAF_URI, null);
	}

	public void setSAF_Uri(String value) {
		//PreferenceManager.getDefaultSharedPreferences(this);
		SharedPreferences.Editor editor = getSharedPreferences().edit();
		editor.putString(PREF_SAF_URI, value);
		editor.commit();
	}

	public String getInstallationDIR() {
		return getSharedPreferences().getString(PREF_INSTALLATION_DIR, null);
	}

	public void setInstallationDIR(String value) {
		SharedPreferences.Editor editor = getSharedPreferences().edit();
		editor.putString(PREF_INSTALLATION_DIR, value);
		editor.commit();
	}

	public String getOldInstallationDIR() {
		return getSharedPreferences().getString(PREF_OLD_INSTALLATION_DIR, null);
	}

	public void setOldInstallationDIR(String value) {
		SharedPreferences.Editor editor = getSharedPreferences().edit();
		editor.putString(PREF_OLD_INSTALLATION_DIR, value);
		editor.commit();
	}


	public String getDefinedControlLayoutLand() {
		return getSharedPreferences().getString(PREF_DEFINED_CONTROL_LAYOUT, null);
	}

	public void setDefinedControlLayoutLand(String value) {
		SharedPreferences.Editor editor = getSharedPreferences().edit();
		editor.putString(PREF_DEFINED_CONTROL_LAYOUT, value);
		editor.commit();
	}

	public String getDefinedControlLayoutPortrait() {
		return getSharedPreferences().getString(PREF_DEFINED_CONTROL_LAYOUT_P, null);
	}

	public void setDefinedControlLayoutPortrait(String value) {
		SharedPreferences.Editor editor = getSharedPreferences().edit();
		editor.putString(PREF_DEFINED_CONTROL_LAYOUT_P, value);
		editor.commit();
	}

	public boolean isTiltSensorEnabled() {

		if (mm.getInputHandler().getGameController().isEnabled())
			return false;

		if (mm.getInputHandler().getMouse().isEnabled())
			return false;

		return getSharedPreferences().getBoolean(PREF_TILT_SENSOR, false);
	}

	public int getTiltSensitivity() {
		return getSharedPreferences().getInt(PREF_TILT_SENSITIVITY, 6);
	}

	public int getTiltVerticalNeutralPos() {
		return Integer.valueOf(getSharedPreferences().getString(PREF_TILT_NEUTRAL, "5")).intValue();
	}

	public int getTiltDZ() {
		return Integer.valueOf(getSharedPreferences().getString(PREF_TILT_DZ, "3")).intValue();
	}

	public boolean isTiltAnalog() {
		return getSharedPreferences().getBoolean(PREF_TILT_ANALOG, true);
	}

	public boolean isTiltTouch() {
		return getSharedPreferences().getBoolean(PREF_TILT_TOUCH, false);
	}

	public boolean isTiltSwappedYZ() {
		return getSharedPreferences().getBoolean(PREF_TILT_SWAP_YZ, false);
	}

	public boolean isTiltInvertedX() {
		return getSharedPreferences().getBoolean(PREF_TILT_INVERT_X, false);
	}

	public int getButtonsAlpha() {
		return Integer.valueOf(getSharedPreferences().getString(PREF_BUTTONS_ALPHA, "60")).intValue();
	}

	public int getButtonsSize() {
		return Integer.valueOf(getSharedPreferences().getString(PREF_BUTTONS_SIZE, "3")).intValue();
	}

	public int getStickSize() {
		return Integer.valueOf(getSharedPreferences().getString(PREF_STICK_SIZE, "3")).intValue();
	}

	public int getMainThreadPriority() {
		return Integer.valueOf(getSharedPreferences().getString(PREF_MAIN_THREAD_PRIORITY, "2")).intValue();
	}

	public int getSoundEngine() {
		return Integer.valueOf(getSharedPreferences().getString(PREF_SOUND_ENGINE, "1")).intValue();
	}

	public boolean isAltGLPath() {
		return getSharedPreferences().getBoolean(PREF_FORCE_ALTGLPATH, false);
	}

	public boolean areSavesInRomPath() {
		return getSharedPreferences().getBoolean(SAVESATES_IN_ROM_PATH, true);
	}

	public boolean isPlayerXasPlayer1() {
		return getSharedPreferences().getBoolean(PREF_PXASP1, false);
	}

	public boolean isOverrideDZandSAT() {
		return getSharedPreferences().getBoolean(PREF_NODEADZONEANDSAT, true);
	}

	public boolean isUsedMAMEini() {
		return getSharedPreferences().getBoolean(PREF_MAMEINI, false);
	}

	public boolean isSpeedHacks() {
		return getSharedPreferences().getBoolean(PREF_SPEED_HACKS, false);
	}

	public boolean isAutofire() {
		return getSharedPreferences().getBoolean(PREF_AUTOFIRE, false);
	}

	public boolean isInputMacro() {
		return getSharedPreferences().getBoolean(PREF_INPUTMACRO, false);
	}

	public boolean isHiscore() {
		return getSharedPreferences().getBoolean(PREF_HISCORE, false);
	}

	public boolean isVectorBeam2x() {
		return getSharedPreferences().getBoolean(PREF_BEAM2X, true);
	}

	public boolean isVectorFlicker() {
		return getSharedPreferences().getBoolean(PREF_FLICKER, false);
	}

	/*public int getEffectOverlayIntensity(){
		return Integer.valueOf(getSharedPreferences().getString(PREF_OVERLAY_INTENSITY,"3")).intValue();
	}*/

	public int getNavBarMode() {

		if (getSharedPreferences().getString(PREF_GLOBAL_NAVBAR_MODE, "").equals("")) {
			String value = PREF_NAVBAR_IMMERSIVE + "";
			SharedPreferences.Editor edit = getSharedPreferences().edit();
			edit.putString(PREF_GLOBAL_NAVBAR_MODE, value);
			edit.commit();
		}

		return Integer.valueOf(getSharedPreferences().getString(PREF_GLOBAL_NAVBAR_MODE, "1")).intValue();
	}

	public boolean isDefaultData() {

		boolean v = getSharedPreferences().getBoolean(PrefsHelper.PREF_MAME_DEFAULTS, false);

		if (v) {
			SharedPreferences.Editor editor = getSharedPreferences().edit();
			editor.putBoolean(PrefsHelper.PREF_MAME_DEFAULTS, false);
			editor.commit();
		}

		return v;
	}

	public boolean isScaleBeyondBoundaries() {
		return getSharedPreferences().getBoolean(PREF_GLOBAL_SCALE_BEYOND, true);
	}

	public boolean isOverscan() {
		return getSharedPreferences().getBoolean("PREF_GLOBAL_OVERSCAN", false);
	}

	public boolean isNotchUsed() {
		return getSharedPreferences().getBoolean(PREF_GLOBAL_USE_NOTCH, false);
	}

	public boolean isSimpleUI() {
		return getSharedPreferences().getBoolean(PREF_GLOBAL_SIMPLE_UI, false);
	}

	public boolean isBottomReload() {
		return getSharedPreferences().getBoolean("PREF_BOTTOM_RELOAD", true);
	}

	public boolean isLightgunLongPress() {
		return getSharedPreferences().getBoolean("PREF_LIGHTGUN_LONGPRESS", true);
	}

	public boolean isFakeID() {
		return getSharedPreferences().getBoolean(PREF_INPUT_FAKE_ID, false);
	}

	public String getShaderEffectSelected() {
		return getSharedPreferences().getString(PREF_SHADER_EFFECT, "none");
	}

	public boolean isShadersEnabled() {
		return getSharedPreferences().getBoolean(PREF_SHADERS_ENABLED, false);
	}

	public boolean isScrapingEnabled() {
		return getSharedPreferences().getBoolean(PREF_SCRAPE_ENABLED, false);
	}

	public boolean isScrapingIcons() {
		return getSharedPreferences().getBoolean(PREF_SCRAPE_ICONS, true);
	}

	public boolean isScrapingSnapshots() {
		return getSharedPreferences().getBoolean(PREF_SCRAPE_SNAPSHOTS, true);
	}

	public boolean isScrapingAll() {
		return getSharedPreferences().getBoolean(PREF_SCRAPE_ALL, false);
	}

	public boolean isScrapingResize() {
		return getSharedPreferences().getBoolean(PREF_SCRAPE_RESIZE, true);
	}


}
