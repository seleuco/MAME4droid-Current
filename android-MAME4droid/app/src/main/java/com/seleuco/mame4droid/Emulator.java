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
import android.opengl.GLSurfaceView;
import android.os.Environment;
import android.os.Process;
import android.util.Log;
import android.util.Size;
import android.view.View;

import com.seleuco.mame4droid.helpers.DialogHelper;
import com.seleuco.mame4droid.helpers.PrefsHelper;
import com.seleuco.mame4droid.input.TouchController;
import com.seleuco.mame4droid.render.IGLRenderer;
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

	//set str
	final static public int SAF_PATH = 1;
	final static public int ROM_NAME = 2;
	final static public int VERSION = 3;
	final static public int OVERLAY_EFECT = 4;
	final static public int CLI_PARAMS = 5;

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

		synchronized (lock1) {
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
		}
	}

	//synchronized
	static public void changeVideo(final int newWidth, final int newHeight, int newVisWidth, int newVisHeight) {

		Log.d("Thread Video", "changeVideo emu_width:" + emu_width + " emu_height: " + emu_height + " newWidth:" + newWidth + " newHeight: " + newHeight + " newVisWidth:" + newVisWidth + " newVisHeight: " + newVisHeight);
		synchronized (lock1) {

			mm.getInputHandler().resetInput(true);

			//if(emu_width!=newWidth || emu_height!=newHeight)
			//{
			emu_width = newWidth;
			emu_height = newHeight;
			emu_visWidth = newVisWidth;
			emu_visHeight = newVisHeight;

			mm.getMainHelper().updateEmuValues();

			mm.runOnUiThread(new Runnable() {
				public void run() {

					//Toast.makeText(mm, "changeVideo newWidth:"+newWidth+" newHeight:"+newHeight+" newVisWidth:"+newVisWidth+" newVisHeight:"+newVisHeight,Toast.LENGTH_SHORT).show();
					mm.overridePendingTransition(0, 0);

					mm.getMainHelper().updateMAME4droid();
					if (mm.getEmuView().getVisibility() != View.VISIBLE)
						mm.getEmuView().setVisibility(View.VISIBLE);
				}
			});
			//}
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
						text = "Tilt sensor is enabled!";
					else if (mm.getPrefsHelper().isTouchLightgun()) {
						if(mm.getPrefsHelper().isTouchLightgunForced())
							text = "Touch lightgun is always enabled!";
							else
							text = "Touch lightgun is auto enabled!";
					}
					else if (mm.getPrefsHelper().isTouchGameMouse()) {
						if(mm.getPrefsHelper().isTouchGameMouseForced())
							text = "Touch mouse is always enabled!";
							else
						    text = "Touch mouse is auto enabled!";
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

	static public void initAudio(int freq, boolean stereo) {

		sound_current_underruns = -1;
		sound_latency_warmup = true;
		sound_frames_to_warmup = 0;
		sound_frames_to_colddown = 0;

		int sampleFreq = freq;
		int samplesPerFrame = freq / 60;
		int bytesPerFrame = (stereo ? 2 : 1) * samplesPerFrame * 2;
		sound_frame_size = samplesPerFrame;

		int channelConfig = stereo ? AudioFormat.CHANNEL_OUT_STEREO : AudioFormat.CHANNEL_OUT_MONO;
		int audioFormatType = AudioFormat.ENCODING_PCM_16BIT;

		boolean hasLowLatencyFeature =
			mm.getPackageManager().hasSystemFeature(PackageManager.FEATURE_AUDIO_LOW_LATENCY);

		sound_isLowLatency_adjust = AudioTrack.getNativeOutputSampleRate(AudioManager.STREAM_MUSIC) == sampleFreq && hasLowLatencyFeature;

		int bufferSize;

		bufferSize = AudioTrack.getMinBufferSize(sampleFreq, channelConfig, audioFormatType);
		Log.d("audio", "Min buffer size:" + bufferSize);
		// Round to next frame
		bufferSize = (((bufferSize + (bytesPerFrame - 1)) / bytesPerFrame) * bytesPerFrame);
		bufferSize += bytesPerFrame;//add a safety frame

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

		if (sound_isLowLatency_adjust)
			trackBuilder.setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY);

		audioTrack = trackBuilder.build();

		audioTrack.play();
	}

	public static void endAudio() {
		if (audioTrack != null) {
			audioTrack.stop();
			audioTrack.release();
		}
		audioTrack = null;
	}

	public static void writeAudio(byte[] b, int sz) {

		if (audioTrack != null) {

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
								mm.getDialogHelper().setInfoMsg("Error opening file...");
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
						Log.d("ACTION_VIEW","XX name: " + fileName);
						Log.d("ACTION_VIEW","XX path: " + path);
						extROM = true;
						String msg = "Launching: " + fileName + "\nMAME4droid (Current) " + versionName + " by D.Valdeita (Seleuco)";
						new WarnWidget.WarnWidgetHelper(mm, msg, 3, Color.GREEN, true);
					}
				}

				mm.getMainHelper().updateEmuValues();

				loadShaders(mm.getMainHelper().getInstallationDIR());

				View emuView = mm.getEmuView();
				((GLSurfaceView)emuView).queueEvent(() -> {
					Emulator.onChooseRenderer(mm.getPrefsHelper().getVideoRenderMode());
					String effect = mm.getPrefsHelper().getShaderEffectSelected();
					Emulator.setShader(effect.equals("none") ? null : effect);
				});

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
	synchronized public static native void setAnalogData(int i, float v1, float v2);
	public static native int getValue(int key, int i);
	public static native String getValueStr(int key, int i);
	public static native void setValue(int key, int i, int value);
	public static native void setValueStr(int key, int i, String value);
	public static native int setKeyData(int keyCode, int keyAction, char keyChar);
	public static native int setMouseData(int i, int mouseAction, int button, float cx, float cy);
	public static native int setTouchData(int i, int touchAction, float cx, float cy);

	public static native void onSurfaceCreated();
	public static native void onDrawFrame();

	public final static int RENDERER_SOFTWARE = 1;
	public final static int RENDERER_GLES2 = 2;

	public static native void onChooseRenderer(int renderer);

	public static native String[] getShaders(int renderer);
	public static native boolean setShader(String shader);
	public static native void loadShaders(String path);
}
