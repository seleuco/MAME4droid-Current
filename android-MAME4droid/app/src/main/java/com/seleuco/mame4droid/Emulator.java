/*
 * This file is part of MAME4droid.
 *
 * Copyright (C) 2026 David Valdeita (Seleuco)
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

package com.seleuco.mame4droid;

import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Paint.Style;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.net.Uri;
import android.os.Environment;
import android.os.Process;
import android.util.Log;
import android.util.Size;
import android.view.View;


import com.seleuco.mame4droid.helpers.DialogHelper;
import com.seleuco.mame4droid.helpers.PrefsHelper;
import com.seleuco.mame4droid.input.TouchController;
import com.seleuco.mame4droid.views.EmulatorViewGL;
import com.seleuco.mame4droid.widgets.WarnWidget;

import java.io.File;

public class Emulator {

	final static public String TAG = "EMULATOR";

	//gets
	final static public int IN_MENU = 1;
	final static public int IN_GAME = 2;
	final static public int NUMBTNS = 3;
	final static public int NUMWAYS = 4;
	final static public int IS_LIGHTGUN = 5;

	//sets
	final static public int EXIT_GAME = 1;

	final static public int EXIT_PAUSE = 2;
	final static public int SHOW_FPS = 3;

	final static public int AUTO_FRAMESKIP = 4;
	final static public int CHEATS = 5;
	final static public int SKIP_GAMEINFO = 6;

	final static public int DISABLE_DRC = 7;

	final static public int DRC_USE_C = 8;

	final static public int SIMPLE_UI = 9;

	final static public int PAUSE = 11;
	final static public int SOUND_VALUE = 13;

	final static public int AUTOSAVE = 16;
	final static public int SAVESTATE = 17;
	final static public int LOADSTATE = 18;

	final static public int OSD_RESOLUTION = 20;
	final static public int EMU_RESOLUTION = 21;

	final static public int ZOOM_TO_WINDOW = 22;

	final static public int PXASP1 = 24;

	final static public int VBEAM2X = 34;
	final static public int VFLICKER = 36;
	final static public int SOUND_OPTIMAL_FRAMES = 48;
	final static public int SOUND_OPTIMAL_SAMPLERATE = 49;
	final static public int SOUND_ENGINE = 50;

	final static public int MOUSE = 60;
	final static public int REFRESH = 61;
	final static public int USING_SAF = 62;
	final static public int SAVESATES_IN_ROM_PATH = 63;

	final static public int WARN_ON_EXIT = 64;

	final static public int IS_MOUSE = 65;

	final static public int KEYBOARD = 66;

	final static public int NUM_PROCESSORS = 67;
	final static public int NODEADZONEANDSAT = 68;
	final static public int MAMEINI = 69;
	final static public int SPEED_HACKS = 70;
	final static public int AUTOFIRE = 71;
	final static public int INPUTMACRO = 72;
	final static public int HISCORE = 73;

	final static public int BITMAP_FILTERING = 74;
	final static public int VECTOR_IMPROVED = 75;
	final static public int FORCE_UNIFONT = 76;

	final static public int NETPLAY_HAS_CONNECTION = 53;
	final static public int NETPLAY_HAS_JOINED = 54;
	final static public int NETPLAY_DELAY = 55;
	final static public int NETPLAY_IN_ROLLBACK = 56;

	//set str
	final static public int SAF_PATH = 1;
	final static public int ROM_NAME = 2;
	final static public int VERSION = 3;
	final static public int OVERLAY_EFECT = 4;
	final static public int CLI_PARAMS = 5;
	final static public int GAME_SELECTED = 6;
	final static public int LANGUAGE = 7;

	//get str
	final static public int MAME_VERSION = 1;

	//KEYS ACTIONS
	final static public int KEY_DOWN = 1;
	final static public int KEY_UP = 2;

	//MOUSE ACTIONS
	final static public int MOUSE_MOVE = 1;
	final static public int MOUSE_BTN_DOWN = 2;
	final static public int MOUSE_BTN_UP = 3;
	final static public int MOUSE_MOVE_POINTER = 4;

	//TOUCH ACTIONS
	final static public int FINGER_MOVE = 1;
	final static public int FINGER_DOWN = 2;
	final static public int FINGER_UP = 3;

	//ANALOG DATA TYPES
	final static public int LEFT_STICK_DATA = 1;
	final static public int RIGHT_STICK_DATA = 2;
	final static public int TRIGGER_DATA = 3;
	final static public int LIGHTGUN_DATA = 4;

	private static MAME4droid mm = null;

	private static boolean isEmulating = false;

	public static boolean isEmulating() {
		return isEmulating;
	}

	private static final Object lock1 = new Object();

	private static boolean emuFiltering = false;

	public static boolean isEmuFiltering() {
		return emuFiltering;
	}

	public static void setEmuFiltering(boolean value) {
		emuFiltering = value;
	}

	private static final Paint debugPaint = new Paint();

	private static int window_width = 320;

	public static int getWindow_width() {
		return window_width;
	}

	private static int window_height = 240;

	public static int getWindow_height() {
		return window_height;
	}

	private static int emu_width = 320;
	private static int emu_height = 240;
	private static int emu_visWidth = 320;
	private static int emu_visHeight = 240;

	private static AudioTrack audioTrack = null;

	private static boolean isDebug = false;

	private static boolean inMenu = false;
	private static boolean oldInMenu = false;

	public static boolean isInGame() {
		return Emulator.getValue(Emulator.IN_GAME) == 1;
	}

	public static boolean isInMenu() {
		return inMenu;
	}

	public static boolean isInGameButNotInMenu() {
		return isInGame() && !isInMenu();
	}

	private static boolean saveorload = false;

	public static void setSaveorload(boolean value) {
		saveorload = value;
	}

	public static boolean isSaveorload() {
		return saveorload;
	}

	private static boolean inOptions = false;

	public static void setInOptions(boolean value) {
		inOptions = value;
	}

	public static boolean isInOptions() {
		return inOptions;
	}

	private static boolean needsRestart = false;

	public static void setNeedRestart(boolean value) {
		needsRestart = value;
	}

	public static boolean isRestartNeeded() {
		return needsRestart;
	}

	private static boolean paused = true;

	public static boolean isPaused() {
		return paused;
	}

	private static boolean portraitFull = false;

	public static boolean isPortraitFull() {
		return portraitFull;
	}

	public static void setPortraitFull(boolean portraitFull) {
		Emulator.portraitFull = portraitFull;
	}

	static {
		try {
			System.loadLibrary("mame4droid-jni");
		} catch (java.lang.Error e) {
			e.printStackTrace();
		}

		debugPaint.setARGB(255, 255, 255, 255);
		debugPaint.setStyle(Style.STROKE);
		debugPaint.setTextSize(16);
	}

	public static int getEmulatedWidth() {
		return emu_width;
	}

	public static int getEmulatedHeight() {
		return emu_height;
	}

	public static int getEmulatedVisWidth() {
		return emu_visWidth;
	}

	public static int getEmulatedVisHeight() {
		return emu_visHeight;
	}

	public static boolean isDebug() {
		return isDebug;
	}

	public static void setDebug(boolean isDebug) {
		Emulator.isDebug = isDebug;
	}

	public static Paint getDebugPaint() {
		return debugPaint;
	}

	public static void setMAME4droid(MAME4droid mm) {
		Emulator.mm = mm;
	}

	//VIDEO
	public static void setWindowSize(int w, int h) {

		//System.out.println("window size "+w+" "+h);

		window_width = w;
		window_height = h;
	}

	//Method to update frame
	static void requestRenderFrame() {

		//synchronized (lock1) {
			try {
				Emulator.inMenu = Emulator.getValue(Emulator.IN_MENU) == 1;

				if (inMenu != oldInMenu) {

					if(!inMenu)
						mm.getInputHandler().resetInput(false);

					if (!inMenu && isSaveorload())
						setSaveorload(false);

					final View v = mm.getInputView();
					if (v != null) {
						mm.runOnUiThread(new Runnable() {
							public void run() {
								v.invalidate();
							}
						});
					}
				}
				oldInMenu = inMenu;
				((EmulatorViewGL) mm.getEmuView()).requestRender();

			} catch (/*Throwable*/NullPointerException t) {
				Log.getStackTraceString(t);
				t.printStackTrace();
			}
		//}
	}



	//synchronized
	static public void changeVideo(final int newWidth, final int newHeight, int newVisWidth, int newVisHeight) {

		Log.d("Thread Video", "changeVideo emu_width:" + emu_width + " emu_height: " + emu_height + " newWidth:" + newWidth + " newHeight: " + newHeight + " newVisWidth:" + newVisWidth + " newVisHeight: " + newVisHeight);

		final java.util.concurrent.CountDownLatch latch = new java.util.concurrent.CountDownLatch(1);

		//synchronized (lock1) {
			mm.getInputHandler().resetInput(true);
			emu_width = newWidth;
			emu_height = newHeight;
			emu_visWidth = newVisWidth;
			emu_visHeight = newVisHeight;
		    mm.getMainHelper().updateEmuValues();
		//}

		mm.runOnUiThread(new Runnable() {
			public void run() {

				if (mm.getEmuView().getVisibility() != View.VISIBLE) {
					mm.getEmuView().setVisibility(View.VISIBLE);
				}

				mm.getMainHelper().updateMAME4droid();

				mm.getEmuView().getViewTreeObserver().addOnGlobalLayoutListener(new android.view.ViewTreeObserver.OnGlobalLayoutListener() {
					@Override
					public void onGlobalLayout() {

						mm.getEmuView().getViewTreeObserver().removeOnGlobalLayoutListener(this);

						latch.countDown();
					}
				});
			}
		});

		try {
			latch.await(3000, java.util.concurrent.TimeUnit.MILLISECONDS);
		} catch (InterruptedException e) {
			e.printStackTrace();
		}
	}

	static public void initInput() {
		Log.d("initInput", "initInput isInGame:" + isInGame() + " isInMenu:" + isInMenu());
		mm.runOnUiThread(new Runnable() {
			public void run() {
				try {
					Thread.sleep(100);
				} catch (InterruptedException e) {
					throw new RuntimeException(e);
				}
				if ( /*Emulator.getValue(Emulator.IN_GAME) == 1 && Emulator.getValue(Emulator.IN_MENU) == 0
					&&*/ (((mm.getPrefsHelper().isTouchLightgun() || mm.getPrefsHelper().isTouchGameMouse())
					&& mm.getInputHandler()
					.getTouchController().getState() != TouchController.STATE_SHOWING_NONE) || mm
					.getPrefsHelper().isTiltSensorEnabled())) {

					CharSequence text = "";
					if (mm.getPrefsHelper().isTiltSensorEnabled())
						text = mm.getString(R.string.tilt_sensor_enabled);
					else if (mm.getPrefsHelper().isTouchLightgun()) {
						if(mm.getPrefsHelper().isTouchLightgunForced())
							text = mm.getString(R.string.touch_lightgun_always);
							else
							text = mm.getString(R.string.touch_lightgun_auto);
					}
					else if (mm.getPrefsHelper().isTouchGameMouse()) {
						if(mm.getPrefsHelper().isTouchGameMouseForced())
							text = mm.getString(R.string.touch_mouse_always);
							else
						    text = mm.getString(R.string.touch_mouse_auto);
					}

					new WarnWidget.WarnWidgetHelper(mm, text.toString(), 3, Color.YELLOW, true);

					Log.d("initInput", "virtual device: " + text);
				}

				mm.getMainHelper().updateMAME4droid();
			}
		});
	}

	//SOUND
	protected static boolean sound_latency_warmup;
	protected static int sound_current_underruns;
	protected static int sound_frames_to_warmup;
	protected static int sound_frames_to_colddown;
	protected static boolean sound_isLowLatency_adjust;
	protected static int sound_frame_size;
	protected static int sound_initial_buffer_size;
	// Netplay-aware audio rebuffering (mirrors opensl_snd.cpp): rollback
	// stalls starve the AudioTrack faster than it refills, so every underrun
	// after the first pops again.
	protected static boolean sound_netplay;
	protected static int sound_np_underruns;
	protected static int sound_bytes_per_frame;
	protected static byte[] sound_np_silence;
	protected static int sound_np_pad_cooldown; // min 1s between pads
	// Adaptive cushion size (frames): starts at 3, grows +1 each time a pad
	// is needed again (cap 8) so slower devices self-tune to their own
	// rollback-starvation length.
	protected static int sound_np_pad_frames;
	// Starvation-episode detection: wall time of the previous writeAudio
	// call, plus a pad-storm counter to escalate to a full track reset
	// when pads keep firing (each pad is itself an audible cut).
	protected static long sound_np_last_write_ms;
	protected static long sound_np_last_pad_ms;
	protected static int sound_np_pad_storm;

	static public void initAudio(int freq, boolean stereo) {

		sound_current_underruns = -1;
		sound_latency_warmup = true;
		sound_frames_to_warmup = 0;
		sound_frames_to_colddown = 0;

		int sampleFreq = freq;
		int samplesPerFrame = freq / 60;
		int bytesPerFrame = (stereo ? 2 : 1) * samplesPerFrame * 2;
		sound_frame_size = samplesPerFrame;
		sound_bytes_per_frame = bytesPerFrame;

		// Netplay session detection (stable for the session: the game always
		// starts after the connection is established).
		sound_netplay = false;
		try { sound_netplay = getValue(NETPLAY_HAS_CONNECTION) == 1; } catch (Throwable t) {}
		sound_np_underruns = 0;
		sound_np_silence = null;
		sound_np_pad_cooldown = 0;
		sound_np_pad_frames = 3;
		sound_np_last_write_ms = 0;
		sound_np_last_pad_ms = 0;
		sound_np_pad_storm = 0;

		int channelConfig = stereo ? AudioFormat.CHANNEL_OUT_STEREO : AudioFormat.CHANNEL_OUT_MONO;
		int audioFormatType = AudioFormat.ENCODING_PCM_16BIT;

		boolean hasLowLatencyFeature =
			mm.getPackageManager().hasSystemFeature(PackageManager.FEATURE_AUDIO_LOW_LATENCY);

		boolean lowLatencyCapable =
			AudioTrack.getNativeOutputSampleRate(AudioManager.STREAM_MUSIC) == sampleFreq && hasLowLatencyFeature;

		// Netplay needs cushion, not minimum latency: skip the warmup SHRINK
		// only, keep PERFORMANCE_MODE_LOW_LATENCY (disabling it pushed some
		// devices to the HAL's chunky deep-buffer path instead).
		sound_isLowLatency_adjust = lowLatencyCapable && !sound_netplay;

		// Log the decisions behind the audio path (e.g. a native-rate mismatch
		// forcing the deep-buffer path) in one logcat line for diagnosis.
		Log.d("audio", "initAudio freq=" + sampleFreq
			+ " nativeRate=" + AudioTrack.getNativeOutputSampleRate(AudioManager.STREAM_MUSIC)
			+ " lowLatencyCapable=" + lowLatencyCapable
			+ " netplay=" + sound_netplay);

		int bufferSize;

		bufferSize = AudioTrack.getMinBufferSize(sampleFreq, channelConfig, audioFormatType);
		Log.d("audio", "Min buffer size:" + bufferSize);
		// Round to next frame
		bufferSize = (((bufferSize + (bytesPerFrame - 1)) / bytesPerFrame) * bytesPerFrame);
		bufferSize += bytesPerFrame;//add a safety frame

		// Extra buffer CAPACITY (not latency) for the adaptive cushion, so a
		// small-buffer device doesn't silently truncate the padding writes.
		if (sound_netplay)
			bufferSize += 8 * bytesPerFrame;

		Log.d("audio", "Effective buffer size:" + bufferSize);

		AudioManager audioManager = (AudioManager) mm.getSystemService(Context.AUDIO_SERVICE);

		AudioAttributes audioAttributes = new AudioAttributes.Builder()
			.setUsage(AudioAttributes.USAGE_GAME)
			//.setContentType(AudioAttributes.CONTENT_TYPE_UNKNOWN)
			.build();

		AudioFormat audioFormat = new AudioFormat.Builder()
			.setSampleRate(sampleFreq)
			.setEncoding(AudioFormat.ENCODING_PCM_16BIT)
			.setChannelMask(stereo ? AudioFormat.CHANNEL_OUT_STEREO : AudioFormat.CHANNEL_OUT_MONO)
			.build();

		AudioTrack.Builder trackBuilder = new AudioTrack.Builder()
			.setAudioFormat(audioFormat)
			.setAudioAttributes(audioAttributes)
			.setTransferMode(AudioTrack.MODE_STREAM)
			.setBufferSizeInBytes(bufferSize);

		if (lowLatencyCapable)
			trackBuilder.setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY);

		audioTrack = trackBuilder.build();

		audioTrack.play();

		// Prime an initial 3-frame silence cushion so the session doesn't start
		// at near-zero fill, where the first scheduler hiccup underruns before
		// any pad has a chance to react.
		if (sound_netplay) {
			byte[] prime = new byte[sound_bytes_per_frame * 3];
			audioTrack.write(prime, 0, prime.length, AudioTrack.WRITE_NON_BLOCKING);
		}
	}

	public static void endAudio() {
		if (audioTrack != null) {
			audioTrack.stop();
			audioTrack.release();
		}
		audioTrack = null;
	}

	// Full AudioTrack reset for netplay starvation episodes: pause+flush+play
	// un-wedges a deeply underrun track, then re-prime the cushion and
	// rebaseline the underrun counter. Emu thread only, no locking needed.
	private static void netplayAudioReset(String reason) {
		try {
			audioTrack.pause();
			audioTrack.flush();
			audioTrack.play();
		} catch (Throwable ignored) {
		}
		byte[] prime = new byte[sound_bytes_per_frame * sound_np_pad_frames];
		audioTrack.write(prime, 0, prime.length, AudioTrack.WRITE_NON_BLOCKING);
		sound_np_underruns = audioTrack.getUnderrunCount();
		sound_np_pad_cooldown = 60;
		sound_np_pad_storm = 0;
		sound_np_last_pad_ms = 0;
		Log.d("audio", "netplay audio reset (" + reason + "), underruns=" + sound_np_underruns);
	}

	public static void writeAudio(byte[] b, int sz) {

		if (audioTrack != null) {

			// Netplay rebuffering, cooldown-gated (1/s): padding on every
			// underrun count increase caused back-to-back silences ("gapped"
			// audio) on bursty devices.
			if (sound_netplay) {
				long now_ms = android.os.SystemClock.uptimeMillis();
				// A long producer gap (resync/initial-sync FF mutes audio, or a
				// network stall) deep-underruns the track; on some HALs it then
				// glitches forever no matter what follows -- reset it clean.
				if (sound_np_last_write_ms != 0 && (now_ms - sound_np_last_write_ms) > 400)
					netplayAudioReset("starvation gap " + (now_ms - sound_np_last_write_ms) + "ms");
				sound_np_last_write_ms = now_ms;
				if (sound_np_pad_cooldown > 0)
					sound_np_pad_cooldown--;
				int u = audioTrack.getUnderrunCount();
				if (u > sound_np_underruns) {
					sound_np_underruns = u;
					if (sound_np_pad_cooldown == 0) {
						sound_np_pad_cooldown = 60; /* ~1s of writeAudio calls */
						// Pad storm: pads re-firing right after their cooldown mean
						// the track itself is stuck glitching (each pad is another
						// audible cut) -- escalate to a full reset.
						if (sound_np_last_pad_ms != 0 && (now_ms - sound_np_last_pad_ms) < 3000)
							sound_np_pad_storm++;
						else
							sound_np_pad_storm = 0;
						sound_np_last_pad_ms = now_ms;
						if (sound_np_pad_storm >= 2) {
							netplayAudioReset("pad storm");
						} else {
							/* Adaptive cushion: another pad means the previous size
							 * was too small for this device's starvation episodes;
							 * grow it (cap 8).                                   */
							if (sound_np_silence != null &&
								sound_np_pad_frames < 8)
								sound_np_pad_frames++;
							sound_np_silence = new byte[sound_bytes_per_frame * sound_np_pad_frames];
							int written = audioTrack.write(sound_np_silence, 0, sound_np_silence.length, AudioTrack.WRITE_NON_BLOCKING);
							// Diagnostic: firing ~every second on an idle session means
							// the cuts ARE the pads themselves (HAL accounting), not
							// rollback starvation.
							Log.d("audio", "netplay pad: underruns=" + u
								+ " padFrames=" + sound_np_pad_frames
								+ " written=" + written + "/" + sound_np_silence.length);
						}
					}
				}
			}

			audioTrack.write(b, 0, sz, AudioTrack.WRITE_NON_BLOCKING);

			if (sound_isLowLatency_adjust) {
				if (sound_frames_to_warmup == 20) {
					if (sound_latency_warmup) {
						sound_initial_buffer_size = audioTrack.getBufferSizeInFrames();
						audioTrack.setBufferSizeInFrames(sound_frame_size * 3);
						sound_latency_warmup = false;
						sound_current_underruns = audioTrack.getUnderrunCount();
						Log.d("audio", "Low latency warm up, underruns:" + audioTrack.getUnderrunCount() + " fr:" + audioTrack.getBufferSizeInFrames());
					} else if (sound_frames_to_colddown < 1000 /*16 seg aprox*/) {
						if (audioTrack.getUnderrunCount() > sound_current_underruns && sound_initial_buffer_size > audioTrack.getBufferSizeInFrames()) {
							int i = audioTrack.getBufferSizeInFrames() + 25;
							audioTrack.setBufferSizeInFrames(i);
							sound_frames_to_colddown = 0;
							Log.d("audio", "Low latency cold down, underruns:" + sound_current_underruns + " fr:" + i);
						}
						sound_current_underruns = audioTrack.getUnderrunCount();
						sound_frames_to_colddown++;
					} else {
						Log.d("audio", "Low latency adjust finish, underruns:" + sound_current_underruns + " fr:" + audioTrack.getBufferSizeInFrames());
						sound_isLowLatency_adjust = false;
					}
				} else {
					sound_frames_to_warmup++;
				}
			}
		}
	}

	//LIVE CYCLE
	public static void pause() {
		//Log.d("EMULATOR", "PAUSE");

		if (audioTrack != null) {
			try {
				audioTrack.pause();
				audioTrack.flush();
			} catch (Throwable ignored) {
			}
		}

		if (isEmulating) {
			//pauseEmulation(true);
			Emulator.setValue(Emulator.PAUSE, 1);
			paused = true;
		}

	}

	public static void resume() {
		//Log.d("EMULATOR", "RESUME");

		if (isRestartNeeded())
			return;

		if (isEmulating) {
			Emulator.setValue(Emulator.PAUSE, 0);
			Emulator.setValue(Emulator.EXIT_PAUSE, 1);
			paused = false;
		}

		if (audioTrack != null)
			audioTrack.play();
	}

	//EMULATOR
	public static void emulate(final String libPath, final String resPath) {

		//Thread.currentThread().setPriority(Thread.MAX_PRIORITY);

		if (isEmulating) return;

		Thread t = new Thread(new Runnable() {

			public void run() {

				int tid=android.os.Process.myTid();
				Log.d(TAG,"priority before change = " + android.os.Process.getThreadPriority(tid));
				boolean err= false;
				try {
					if (mm.getPrefsHelper().getMainThreadPriority() == PrefsHelper.LOW) {
						android.os.Process.setThreadPriority(Process.THREAD_PRIORITY_LESS_FAVORABLE);
					} else if (mm.getPrefsHelper().getMainThreadPriority() == PrefsHelper.NORMAL) {
						android.os.Process.setThreadPriority(Process.THREAD_PRIORITY_MORE_FAVORABLE);
					} else
						android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_DISPLAY);
				} catch (Exception e) {
				}
				Log.d(TAG,"priority after change = " + android.os.Process.getThreadPriority(tid));

				boolean extROM = false;
				Size sz = mm.getMainHelper().getWindowSize();
				init(libPath, resPath, Math.max(sz.getWidth(), sz.getHeight()), Math.min(sz.getWidth(), sz.getHeight()));
				final String versionName = mm.getMainHelper().getVersion();
				Emulator.setValueStr(Emulator.VERSION, versionName);

				/* Start MAME's own UI in the app language (loads the matching
				 * language/<name>/strings.mo shipped in the assets bundle), unless
				 * the user chose to keep MAME's core in English (its own
				 * translations are patchy). The Android app UI is unaffected. */
				String mameLanguage = mm.getPrefsHelper().isMameCoreTranslated()
						? com.seleuco.mame4droid.helpers.LocaleHelper.getMameLanguage(mm)
						: "English";
				Emulator.setValueStr(Emulator.LANGUAGE, mameLanguage);

				boolean isUsingSaf = mm.getPrefsHelper().getROMsDIR() != null && mm.getPrefsHelper().getROMsDIR().length() != 0;
				if (isUsingSaf) {
					Emulator.setValue(Emulator.USING_SAF, 1);
					Emulator.setValueStr(Emulator.SAF_PATH, mm.getPrefsHelper().getROMsDIR());
					mm.getSAFHelper().listUriFiles(true);
					if(mm.getPrefsHelper().isScrapingEnabled())
						mm.getScraperHelper().initMediaScrap();
				}

				Intent intent = mm.getIntent();
				String action = intent.getAction();
				//Uri pkg = null;
				String fileName = null;
				String cliParams = null;
				String path = null;
				boolean delete = false;
				if (Intent.ACTION_VIEW.equals(action)) {
					//android.os.Debug.waitForDebugger();
					//pkg = mm.getReferrer();
					//System.out.println("PKG: "+pkg.getHost());

					Uri _uri = intent.getData();
					Log.d("ACTION_VIEW", "URI = " + _uri);

					cliParams = intent.getStringExtra("cli_params");
					Log.d("ACTION_VIEW", "CLI_PARAMS = " + cliParams);

					boolean error = false;
					try {
						if (_uri != null && "content".equalsIgnoreCase(_uri.getScheme())) {
							//mm.safHelper.setURI(null);//disable SAF.
							//Log.d("SAF","Disabling SAF");
							fileName = mm.getMainHelper().getFileName(_uri);
							String state = Environment.getExternalStorageState();

							if (Environment.MEDIA_MOUNTED.equals(state)) {
								//path = mm.getExternalCacheDir().getPath();
								path = mm.getPrefsHelper().getInstallationDIR() + "roms";
								File f = new File(path + "/" + fileName);
								if (!f.exists()) {
									java.io.InputStream input = mm.getContentResolver().openInputStream(_uri);
									error = mm.getMainHelper().copyFile(input, path, fileName);
									delete = true;
								}
							} else
								error = true;
						} else {
							if(_uri != null)
							{
								String filePath = _uri.getPath();
								if(filePath != null) {
									java.io.File f = new java.io.File(filePath);
									fileName = f.getName();
									path = f.getAbsolutePath().substring(0, f.getAbsolutePath().lastIndexOf(File.separator));
								}
							}
						}
					} catch (Exception e) {
						error = true;
					}

					if (error) {
						mm.runOnUiThread(new Runnable() {
							public void run() {
								mm.getDialogHelper().setInfoMsg(mm.getString(R.string.error_opening_file));
								mm.showDialog(DialogHelper.DIALOG_INFO);
							}
						});
					} else {
						//String cliParams = "-skip_gameinfo -cass gng -autoboot_delay 2 -autoboot_command 'load\\n'";
						//String cliParams = "-skip_gameinfo -cass gng -autoboot_delay 2 -autoboot_command 'LOAD \"*\",8\\n'";
						if(cliParams!=null)
						{
							Emulator.setValueStr(Emulator.CLI_PARAMS, cliParams);
						}

						Emulator.setValueStr(Emulator.ROM_NAME, fileName);
						
						String gameSelected = fileName;
						if (gameSelected.toLowerCase().endsWith(".zip")) {
							gameSelected = gameSelected.substring(0, gameSelected.length() - 4);
						} else if (gameSelected.toLowerCase().endsWith(".7z")) {
							gameSelected = gameSelected.substring(0, gameSelected.length() - 3);
						}
						Emulator.setValueStr(Emulator.GAME_SELECTED, gameSelected);

						Log.d("ACTION_VIEW","XX name: " + fileName);
						Log.d("ACTION_VIEW","XX path: " + path);
						extROM = true;
						String msg = mm.getString(R.string.launching_rom, fileName, versionName);
						new WarnWidget.WarnWidgetHelper(mm, msg, 3, Color.GREEN, true);
					}
				}

				mm.getMainHelper().updateEmuValues();

				View emuView = mm.getEmuView();

				isEmulating = true;
				runT();

				if (extROM) {

					if (delete) {
						java.io.File f = new java.io.File(path, fileName);
						f.delete();
					}
				}
				mm.runOnUiThread(new Runnable() {
					public void run() {
						mm.finishAndRemoveTask();
						android.os.Process.killProcess(android.os.Process.myPid());
					}
				});
			}
		}, "emulatorNativeMain-Thread");

/*
		if (mm.getPrefsHelper().getMainThreadPriority() == PrefsHelper.LOW) {
			t.setPriority(Thread.MIN_PRIORITY);
		} else if (mm.getPrefsHelper().getMainThreadPriority() == PrefsHelper.NORMAL) {
			t.setPriority(Thread.NORM_PRIORITY);
		} else
			t.setPriority(Thread.MAX_PRIORITY);
*/
		t.start();
	}

	public static int getValue(int key) {
		return getValue(key, 0);
	}

	public static String getValueStr(int key) {
		return getValueStr(key, 0);
	}

	public static void setValue(int key, int value) {
		setValue(key, 0, value);
	}

	public static void setValueStr(int key, String value) {
		setValueStr(key, 0, value);
	}

	static int safOpenFile(String pathName, String mode) {
		//System.out.println("-->Llaman a safOpenFile en java "+pathName+" "+mode);

		String file = "";

		String romPath = mm.getPrefsHelper().getROMsDIR();
		if (pathName.startsWith(romPath))
			file = pathName.substring(romPath.length() + 1, pathName.length());

		if (file.equals(""))
			return -1;

		//System.out.println("File with path "+file);

		return mm.getSAFHelper().openUriFd("/" + file, mode);
	}

	static int safReadDir(String dirName, int reload) {
		//System.out.println("Llaman a safReadDir en java "+dirName);

		//boolean res = mm.getSAFHelper().listUriFiles(reload == 1);

		String dirSAF = "";

		String romPath = mm.getPrefsHelper().getROMsDIR();
		if (dirName.startsWith(romPath)) {
			dirSAF = dirName.substring(romPath.length(), dirName.length());
			if (!dirSAF.startsWith("/")) dirSAF = "/" + dirSAF;
			if (!dirSAF.endsWith("/")) dirSAF = dirSAF + "/";
		}

		return mm.getSAFHelper().readDir(dirSAF);
	}

	static String[] safGetNextDirEntry(int dirId) {
		//System.out.println("Llaman a safGetNextDirEntry en java "+dirId);
		return mm.getSAFHelper().getNextDirEntry(dirId);
	}

	static void safCloseDir(int dirId) {
		//System.out.println("Llaman a safCloseDir en java "+dirId);
		mm.getSAFHelper().closeDir(dirId);
	}

	//native
	protected static native void init(String libPath, String resPath, int nativeWidth, int nativeHeight);
	protected static native void runT();
	synchronized public static native void setDigitalData(int i, long data);
	synchronized public static native void setAnalogData(int t, int i, float v1, float v2);
	public static native int getValue(int key, int i);
	public static native String getValueStr(int key, int i);
	public static native void setValue(int key, int i, int value);
	public static native void setValueStr(int key, int i, String value);
	public static native int setKeyData(int keyCode, int keyAction, char keyChar);
	public static native int setMouseData(int i, int mouseAction, int button, float cx, float cy);
	public static native int setTouchData(int i, int touchAction, float cx, float cy);

	public final static int RENDERER_GL_SW = 1;
	public final static int RENDERER_GL_NATIVE = 2;

	public static native int newRenderer();
	public static native int onDrawFrame(int renderer, int isHdr);

	public static native String[] getShaders();
	public static native boolean setShader(String shader);
	public static native int loadShaders(String path);

	public static native void setRendererParameters(String[] keys, String[] values);

	public static native int netplayInit(String server, int port, int join);

	/** Set the netplay mode: 0 = LOCKSTEP (default), 1 = ROLLBACK.
	 *  Must be called before {@link #netplayInit}. */
	public static native void netplaySetMode(int mode);

	/** Enable/disable the CRC desync detector (saves its per-frame compute
	 *  cost when off). Local to this device; must be called before
	 *  {@link #netplayInit}. */
	public static native void netplaySetDesyncDetectorEnabled(int enabled);

	/** Internet play: peer public tuple the host hole-punches toward.
	 *  Callable before {@link #netplayInit} and hot while waiting for the
	 *  peer; null/empty clears it. */
	public static native void netplaySetPunchAddr(String addr, int port);

	/** Internet play: run STUN on the game socket during the next
	 *  {@link #netplayInit} (adds up to ~3s: call init from a worker). */
	public static native void netplaySetInternetMode(int on);

	/** Local bind port for the next {@link #netplayInit}: always OUR settings
	 *  port -- the join target's port must never leak into our own tuple. */
	public static native void netplaySetLocalPort(int port);

	/** "ip:port|pp=0/1|sym=0/1" or "" -- valid after {@link #netplayInit}
	 *  returns, until the next init. */
	public static native String netplayGetPublicAddr();

	/** Multi-line connection diagnostics block; same validity as
	 *  {@link #netplayGetPublicAddr}. */
	public static native String netplayGetDiagnostics();

	/** One-shot STUN to learn OUR public IP before joining (same-router vs
	 *  192.168.x collision).  BLOCKS up to ~1.5s: call off the UI thread.
	 *  Returns the IP, or "" when offline / STUN blocked. */
	public static native String netplayProbePublicIp();

	/** Latch a mid-game state resync (rollback sessions only).
	 *  The host recaptures its live state and streams it to the client, which
	 *  adopts it -- both machines freeze briefly and resume bit-identical.
	 *  @return 1 if the resync was latched, 0 if not applicable. */
	public static native int netplayResync();

	/* Native netplay toasts arrive as "@key|arg1|arg2": the key maps to a
	 * localized string resource and the args fill its placeholders, so the
	 * message shows in the user's language.  Plain text (no leading '@') is
	 * passed through unchanged for forward/backward compatibility. */
	private static String resolveNpMsg(String body) {
		if (body == null || !body.startsWith("@"))
			return body;
		String[] parts = body.substring(1).split("\\|", -1);
		String key = parts[0];
		Object[] args = new Object[parts.length - 1];
		System.arraycopy(parts, 1, args, 0, args.length);
		int resId = 0;
		switch (key) {
			case "peer_paused":            resId = R.string.np_msg_peer_paused; break;
			case "incompatible":           resId = R.string.np_msg_incompatible; break;
			case "desync_rollback":        resId = R.string.np_msg_desync_rollback; break;
			case "desync":                 resId = R.string.np_msg_desync; break;
			case "state_sync_failed":      resId = R.string.np_msg_state_sync_failed; break;
			case "host_running":           resId = R.string.np_msg_host_running; break;
			case "peer_resync":            resId = R.string.np_msg_peer_resync; break;
			case "not_rollback_compatible":resId = R.string.np_msg_not_rollback_compatible; break;
			case "sync_timeout":           resId = R.string.np_msg_sync_timeout; break;
			case "resyncing":              resId = R.string.np_msg_resyncing; break;
			case "state_too_large":        resId = R.string.np_msg_state_too_large; break;
			case "disconnected":           resId = R.string.np_msg_disconnected; break;
			case "rom_error":              resId = R.string.np_msg_rom_error; break;
			case "connection_lost":        resId = R.string.np_msg_connection_lost; break;
			case "peer_disconnected":      resId = R.string.np_msg_peer_disconnected; break;
			case "send_failed":            resId = R.string.np_msg_send_failed; break;
			case "bind_failed":            resId = R.string.np_msg_bind_failed; break;
			default: return body;
		}
		try {
			return mm.getString(resId, args);
		} catch (Exception e) {
			return body;
		}
	}

	static void netplayWarn(final String msg) {
		mm.runOnUiThread(new Runnable() {
			public void run() {
				if (msg != null && msg.startsWith("TOASTERR:")) {
					new com.seleuco.mame4droid.widgets.WarnWidget.WarnWidgetHelper(mm, resolveNpMsg(msg.substring(9)), 4, android.graphics.Color.RED, false);
				} else if (msg != null && msg.startsWith("TOASTOK:")) {
					new com.seleuco.mame4droid.widgets.WarnWidget.WarnWidgetHelper(mm, resolveNpMsg(msg.substring(8)), 3, android.graphics.Color.GREEN, false);
				} else if (msg != null && msg.startsWith("TOAST:")) {
					new com.seleuco.mame4droid.widgets.WarnWidget.WarnWidgetHelper(mm, resolveNpMsg(msg.substring(6)), 3, android.graphics.Color.YELLOW, false);
				} else if (msg != null && msg.startsWith("STATS:")) {
					/* Re-check the connection live (not just at push time): a
					 * STATS push and the disconnect notification can both be
					 * in-flight around the same moment, so a stale push must
					 * not resurrect the overlay after it's already hidden. */
					if (mm.getPrefsHelper().isNetplayStatsEnabled() && getValue(NETPLAY_HAS_CONNECTION) == 1)
						com.seleuco.mame4droid.widgets.StatsWidget.update(mm, msg.substring(6));
					else
						com.seleuco.mame4droid.widgets.StatsWidget.hide(mm);
				} else {
					/* Unprefixed native warnings (disconnect, hangup, socket
					 * errors) that are shown as a MODAL dialog.  Still resolve a
					 * leading "@key" so the modal text is localized; plain text
					 * passes through unchanged.                                  */
					mm.getDialogHelper().setInfoMsg(resolveNpMsg(msg));
					mm.showDialog(DialogHelper.DIALOG_INFO);
				}
				/* Native netplay notifications (hangup, peer timeout,
				 * disconnect...) are the moment to re-check the session state
				 * and drop the Wi-Fi radio lock if the connection is gone --
				 * no string matching, just the authoritative flag.           */
				try {
					if (mm.getNetPlay() != null
							&& getValue(NETPLAY_HAS_CONNECTION) == 0) {
						mm.getNetPlay().releaseWifiLock();
						mm.getNetPlay().onNetplaySessionGone();
						com.seleuco.mame4droid.widgets.StatsWidget.hide(mm);
					}
				} catch (Throwable ignored) {}
			}
		});
	}
}
