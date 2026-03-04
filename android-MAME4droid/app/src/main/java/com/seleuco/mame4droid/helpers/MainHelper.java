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

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.StringTokenizer;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

import android.app.Activity;
import android.app.AlarmManager;
import android.app.PendingIntent;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager.NameNotFoundException;
import android.content.res.Configuration;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.Point;
import android.graphics.Rect;
import android.media.AudioManager;
import android.net.Uri;
import android.opengl.GLSurfaceView;
import android.os.Build;
import android.os.Environment;
import android.preference.PreferenceManager;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.util.Log;
import android.util.Size;
import android.view.Display;
import android.view.Gravity;
import android.view.View;
import android.view.View.MeasureSpec;
import android.view.WindowInsets;
import android.view.WindowMetrics;
import android.widget.FrameLayout;
import android.widget.Toast;

import com.seleuco.mame4droid.Emulator;
import com.seleuco.mame4droid.MAME4droid;
import com.seleuco.mame4droid.R;
import com.seleuco.mame4droid.WebHelpActivity;
import com.seleuco.mame4droid.input.ControlCustomizer;
import com.seleuco.mame4droid.input.GameController;
import com.seleuco.mame4droid.input.InputHandler;
import com.seleuco.mame4droid.input.TouchController;
import com.seleuco.mame4droid.prefs.UserPreferences;
import com.seleuco.mame4droid.widgets.WarnWidget;
import com.seleuco.mame4droid.views.IEmuView;
import com.seleuco.mame4droid.views.InputView;

public class MainHelper {

    final static public int SUBACTIVITY_USER_PREFS = 1;
    final static public int SUBACTIVITY_HELP = 2;
    final static public int BUFFER_SIZE = 1024 * 48;

    // final static public String MAGIC_FILE = "dont-delete-00005.bin";

    final public static int DEVICE_GENERIC = 1;
    final public static int DEVICE_SHIELD = 3;
    final public static int DEVICE_AGAMEPAD2 = 5;
    final public static int DEVICE_ANDROIDTV = 5;
	final public static int DEVICE_METAQUEST = 6;

    final public static int INSTALLATION_DIR_UNDEFINED = 1;
    final public static int INSTALLATION_DIR_FILES_DIR = 2;
    final public static int INSTALLATION_DIR_LEGACY = 3;
    final public static int INSTALLATION_DIR_MEDIA_FOLDER = 4;

    protected int installationDirType = INSTALLATION_DIR_UNDEFINED;

    protected boolean createdInstallationDir = false;

    protected int deviceDetected = DEVICE_GENERIC;

    protected int oldInGame = 0;

	protected int oldState = -1;

    final public static int REQUEST_CODE_OPEN_DIRECTORY = 33;

    public int getDeviceDetected() {
        return deviceDetected;
    }

    protected MAME4droid mm = null;

    public MainHelper(MAME4droid value) {
        mm = value;
    }

    public void setInstallationDirType(int installationDirType) {
        this.installationDirType = installationDirType;
    }

    public int getInstallationDirType() {
        return installationDirType;
    }

    public boolean isCreatedInstallationDir() {
        return createdInstallationDir;
    }

    public String getLibDir() {
        String cache_dir, lib_dir;
        try {
            // cache_dir = mm.getCacheDir().getCanonicalPath();
            // lib_dir = cache_dir.replace("cache", "lib");
            lib_dir = mm.getApplicationInfo().nativeLibraryDir;
        } catch (Exception e) {
            e.printStackTrace();
            lib_dir = "/data/data/com.seleuco.mame4droid/lib";
        }
        return lib_dir;
    }

    public String getInstallationDIR() {
        String res_dir = null;

        if (mm.getPrefsHelper().getInstallationDIR() != null)
            return mm.getPrefsHelper().getInstallationDIR();

        // android.os.Debug.waitForDebugger();
        String state = Environment.getExternalStorageState();
        if (Environment.MEDIA_MOUNTED.equals(state)) {
            if (getInstallationDirType() == INSTALLATION_DIR_FILES_DIR)
                res_dir = mm.getExternalFilesDir(null).getAbsolutePath() + "/";
            else if (getInstallationDirType() == INSTALLATION_DIR_LEGACY)
                res_dir = Environment.getExternalStorageDirectory().getAbsolutePath() + "/MAME4droid/";
            else if (getInstallationDirType() == INSTALLATION_DIR_MEDIA_FOLDER)
            {
                File[] dirs = mm.getExternalMediaDirs();
                for(File d: dirs)
                {
                    if(d != null) {
                        res_dir = d.getAbsolutePath() + "/";
                        break;
                    }
                }
            }
        }
        if (res_dir == null)
            res_dir = mm.getFilesDir().getAbsolutePath() + "/";

        // res_dir =
        // mm.getExternalFilesDir(null).getAbsolutePath()+"/MAME4droid/";
        // File[] f = mm.getExternalFilesDirs(null);
        // res_dir = f[f.length-1].getAbsolutePath();

        mm.getPrefsHelper().setInstallationDIR(res_dir);

        return res_dir;
    }

    public boolean ensureInstallationDIR(String dir) {

        if (!dir.endsWith("/"))
            dir += "/";

        File res_dir = new File(dir);

        boolean created = false;

        if (res_dir.exists() == false) {
            if (!res_dir.mkdirs()) {
                mm.getDialogHelper().setErrorMsg(
                        "Can't find/create: '" + dir + "' Is it writable?.\nReverting...");
                mm.showDialog(DialogHelper.DIALOG_ERROR_WRITING);
                return false;
            } else {
                created = true;
            }
        }

        String str_sav_dir = dir + "saves/";
        File sav_dir = new File(str_sav_dir);
        if (sav_dir.exists() == false) {
            if (!sav_dir.mkdirs()) {
                mm.getDialogHelper().setErrorMsg(
                        "Can't find/create: '" + str_sav_dir + "' Is it writable?.\nReverting...");
                mm.showDialog(DialogHelper.DIALOG_ERROR_WRITING);
                return false;
            } else {
                created = true;
            }
        }

		String str_dummy_file = dir + "saves/dummy.txt";
		File dummy_file = new File(str_dummy_file);
		if(!dummy_file.exists())
		{
			try {
				dummy_file.createNewFile();
			} catch (IOException e) {
				mm.getDialogHelper().setErrorMsg(
					"Can't find/create: '" + dummy_file + "' Is it writable?.\nReverting...");
				mm.showDialog(DialogHelper.DIALOG_ERROR_WRITING);
				return false;
			}
		}

		createdInstallationDir = created;

        mm.getPrefsHelper().setOldInstallationDIR(dir);

        return true;
    }

    protected boolean deleteRecursive(File path) throws FileNotFoundException {
        if (!path.exists())
            throw new FileNotFoundException(path.getAbsolutePath());
        boolean ret = true;
        if (path.isDirectory()) {
            for (File f : path.listFiles()) {
                ret = ret && deleteRecursive(f);
            }
        }
        return ret && path.delete();
    }

    public void removeFiles() {
        try {
            if (mm.getPrefsHelper().isDefaultData()) {
                String dir = mm.getMainHelper().getInstallationDIR();

				File a = new File(dir + File.separator + "ui/mame_avail.ini");
				a.delete();

				File b = new File(dir + File.separator + "mame.ini");
				b.delete();

                File f1 = new File(dir + File.separator + "cfg/");
                File f2 = new File(dir + File.separator + "nvram/");

                deleteRecursive(f1);
                deleteRecursive(f2);

                Toast.makeText(mm, "Deleted MAME cfg and NVRAM files...",
                        Toast.LENGTH_LONG).show();
            }

        } catch (Exception e) {
            /*Toast.makeText(mm, "Failed deleting:" + e.getMessage(),
                    Toast.LENGTH_LONG).show();*/
            e.printStackTrace();
        }
    }

    public void copyFiles() {

		WarnWidget pw = null;

		try {

            String roms_dir = mm.getMainHelper().getInstallationDIR();

            File fm = new File(roms_dir + File.separator + "saves/"
                    + "dont-delete-" + getVersion() + ".bin");
            if (fm.exists())
                return;

            fm.mkdirs();
            fm.createNewFile();

			pw = new WarnWidget(mm,"Installing files, ","please wait...", Color.WHITE,false,true);
			pw.init();

            // Create a ZipInputStream to read the zip file
            BufferedOutputStream dest = null;
            InputStream fis = mm.getResources().openRawResource(R.raw.files);
            ZipInputStream zis = new ZipInputStream(new BufferedInputStream(fis));
            // Loop over all of the entries in the zip file

            String zip_dir = new File(roms_dir).getCanonicalPath();
            int count;
            byte data[] = new byte[BUFFER_SIZE];
            ZipEntry entry;
            while ((entry = zis.getNextEntry()) != null) {
                if (!entry.isDirectory()) {

                    File f = new File(zip_dir, entry.getName());
                    String canonicalPath = f.getCanonicalPath();
                    if (!canonicalPath.startsWith(zip_dir)) {
                        throw new SecurityException("Error zip!!!!");
                    }

					pw.notifyText("Installing: "+f.getName());

                    String destination = zip_dir;
                    String destFN = destination + File.separator + entry.getName();
                    // Write the file to the file system
                    FileOutputStream fos = new FileOutputStream(destFN);
                    dest = new BufferedOutputStream(fos, BUFFER_SIZE);
                    while ((count = zis.read(data, 0, BUFFER_SIZE)) != -1) {
                        dest.write(data, 0, count);
                    }
                    dest.flush();
                    dest.close();
                } else {
                    File f = new File(zip_dir+ File.separator
                            + entry.getName());
                    f.mkdirs();
                }

            }
            zis.close();

			File f1 = new File(zip_dir, "ui.ini");
			if(!f1.exists())
			{
				File f2 = new File(zip_dir, "ui.ini.bak");
				f2.renameTo(f1);
			}

			pw.end();
			pw=null;

            String dir = this.getInstallationDIR();
            if (!dir.endsWith("/")) dir += "/";
            String rompath = mm.getPrefsHelper().getROMsDIR() != null && mm.getPrefsHelper().getROMsDIR() != "" ? mm
                    .getPrefsHelper().getROMsDIR() : dir + "roms";
            String msg =
                    "Created or updated: '"
                            + dir
                            + "' to store save states, cfg files and MAME assets.\n\nNote, copy or move your zipped ROMs under '"
                            + rompath
                            + "' directory!\n\nIMPORTANT: MAME4droid (Current) uses only "+ mm.getString(R.string.mame_version) +" MAME romset, not 0.139.";

			if(mm.getMainHelper().getDeviceDetected() == MainHelper.DEVICE_METAQUEST){
				msg = "Welcome to MAME4droid for META Quest!\n\nYou should pair an XBOX One controller on your META Quest to be able to play games but you can play lightgun games with your touch controllers.\n\n"+msg;
			}

            //if (mm.getPrefsHelper().getSAF_Uri()!=null)
                //msg += "\n\nTIP: You can enable a setting to store save states under roms folder, so they will not be deleted when uninstalling MAME4droid. Look at MAME4droid option in settings.";
            mm.getDialogHelper().setInfoMsg(msg);

			mm.runOnUiThread(new Runnable() {
				public void run() {
					try {
						Thread.sleep(1000);
					} catch (InterruptedException e) {

					}
					mm.showDialog(DialogHelper.DIALOG_INFO);
				}
			});
            //mm.showDialog(DialogHelper.DIALOG_INFO);

        } catch (Exception e) {
            e.printStackTrace();
			if(pw!=null)
				pw.end();
        }
    }

    public int getscrOrientation() {
        Display getOrient = mm.getWindowManager().getDefaultDisplay();
        // int orientation = getOrient.getOrientation();

        int orientation = mm.getResources().getConfiguration().orientation;

        // Sometimes you may get undefined orientation Value is 0
        // simple logic solves the problem compare the screen
        // X,Y Co-ordinates and determine the Orientation in such cases
        if (orientation == Configuration.ORIENTATION_UNDEFINED) {

            Configuration config = mm.getResources().getConfiguration();
            orientation = config.orientation;

            if (orientation == Configuration.ORIENTATION_UNDEFINED) {
                // if emu_height and widht of screen are equal then
                // it is square orientation
                if (getOrient.getWidth() == getOrient.getHeight()) {
                    orientation = Configuration.ORIENTATION_SQUARE;
                } else { // if widht is less than emu_height than it is portrait
                    if (getOrient.getWidth() < getOrient.getHeight()) {
                        orientation = Configuration.ORIENTATION_PORTRAIT;
                    } else { // if it is not any of the above it will defineitly
                        // be landscape
                        orientation = Configuration.ORIENTATION_LANDSCAPE;
                    }
                }
            }
        }
        return orientation; // return values 1 is portrait and 2 is Landscape
        // Mode
    }

    public void reload() {

        if (true)
            return;
        System.out.println("RELOAD!!!!!");

        Intent intent = mm.getIntent();
        System.out.println("RELOAD intent:" + intent.getAction());

        mm.overridePendingTransition(0, 0);
        intent.addFlags(Intent.FLAG_ACTIVITY_NO_ANIMATION);
        mm.finish();

        mm.overridePendingTransition(0, 0);
        mm.startActivity(intent);
        mm.overridePendingTransition(0, 0);
    }

    public void updateEmuValues() {

        PrefsHelper prefsHelper = mm.getPrefsHelper();

        Emulator.setValue(Emulator.SHOW_FPS,
                prefsHelper.isFPSShowed() ? 1 : 0);

		Emulator.setValue(Emulator.ZOOM_TO_WINDOW,
			prefsHelper.isZoomToWindow() ? 1 : 0);

		Emulator.setValue(Emulator.AUTO_FRAMESKIP,
			prefsHelper.isAutoFrameSkip() ? 1 : 0);

		Emulator.setValue(Emulator.CHEATS,
			prefsHelper.isCheats() ? 1 : 0);

		Emulator.setValue(Emulator.SKIP_GAMEINFO,
			prefsHelper.isSkipGameInfo() ? 1 : 0);

		Emulator.setValue(Emulator.DISABLE_DRC,
			prefsHelper.isDisabledDRC() ? 1 : 0);

		Emulator.setValue(Emulator.DRC_USE_C,
			prefsHelper.isDRCUseC() ? 1 : 0);

		Emulator.setValue(Emulator.NUM_PROCESSORS,
			prefsHelper.getNumProcessors());

		Emulator.setValue(Emulator.SIMPLE_UI,
			prefsHelper.isSimpleUI() ? 1 : 0);

        Emulator.setValue(Emulator.EMU_RESOLUTION,
                prefsHelper.getEmulatedResolution());

		Emulator.setValue(Emulator.OSD_RESOLUTION,
			prefsHelper.getOSDResolution());

		Emulator.setValue(Emulator.WARN_ON_EXIT,
			prefsHelper.isWarnOnExit() ? 1 : 0);

        Emulator.setValue(Emulator.PXASP1, mm.getPrefsHelper()
                .isPlayerXasPlayer1() ? 1 : 0);
		Emulator.setValue(Emulator.NODEADZONEANDSAT, mm.getPrefsHelper()
			.isOverrideDZandSAT() ? 1 : 0);

		Emulator.setValue(Emulator.MAMEINI, mm.getPrefsHelper()
			.isUsedMAMEini() ? 1 : 0);

		Emulator.setValue(Emulator.SPEED_HACKS, mm.getPrefsHelper()
			.isSpeedHacks() ? 1 : 0);

		Emulator.setValue(Emulator.AUTOFIRE, mm.getPrefsHelper()
			.isAutofire() ? 1 : 0);

		Emulator.setValue(Emulator.INPUTMACRO, mm.getPrefsHelper()
			.isInputMacro() ? 1 : 0);

		Emulator.setValue(Emulator.HISCORE, mm.getPrefsHelper()
			.isHiscore() ? 1 : 0);

        Emulator.setValue(Emulator.VBEAM2X, mm.getPrefsHelper()
                .isVectorBeam2x() ? 1 : 0);
        Emulator.setValue(Emulator.VFLICKER, mm.getPrefsHelper()
                .isVectorFlicker() ? 1 : 0);

        Emulator.setValue(
                Emulator.SAVESATES_IN_ROM_PATH,
                mm.getPrefsHelper().areSavesInRomPath() ? 1 : 0);

        Emulator.setValue(Emulator.MOUSE,
                mm.getPrefsHelper().isMouseEnabled() || mm.getPrefsHelper().isTouchMouseEnabled() ? 1 : 0);

		Emulator.setValue(Emulator.KEYBOARD,
			mm.getPrefsHelper().isKeyboardEnabled() || mm.getPrefsHelper().isVirtualKeyboardEnabled() ? 1 : 0);

        Emulator.setValue(Emulator.SOUND_ENGINE, mm.getPrefsHelper()
                .getSoundEngine() > 2 ? 2 : 1);

        AudioManager am = (AudioManager) mm
                .getSystemService(Context.AUDIO_SERVICE);
        int sfr = 512;//10ms a 48000khz

        if (mm.getPrefsHelper().getSoundEngine() == PrefsHelper.PREF_SNDENG_OPENSL_LOW) {
            try {
                sfr = Integer
                        .valueOf(
                                am.getProperty(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER))
                        .intValue();
                System.out.println("PROPERTY_OUTPUT_FRAMES_PER_BUFFER:" + sfr);
            } catch (Throwable e) {
            }
        }

        Emulator.setValue(Emulator.SOUND_OPTIMAL_FRAMES, sfr);

        int sr = 44100;

        try {
            sr = Integer.valueOf(
                    am.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE))
                    .intValue();
            System.out.println("PROPERTY_OUTPUT_SAMPLE_RATE:" + sr);
        } catch (Throwable e) {
        }

        Context context = mm.getApplicationContext();
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(context);
        if (!prefs.getBoolean("sound_rate", false)) {
            SharedPreferences.Editor edit = prefs.edit();
            edit.putBoolean("sound_rate", true);
            if (sr == 48000)//sino defecto 44100
                edit.putString(PrefsHelper.PREF_EMU_SOUND, sr + "");
            edit.commit();
        }

        if (mm.getPrefsHelper().getSoundEngine() == PrefsHelper.PREF_SNDENG_OPENSL)
            sr = mm.getPrefsHelper().getSoundValue();
		/*
		else is PrefsHelper.PREF_SNDENG_OPENSL_LOW fixed at PROPERTY_OUTPUT_SAMPLE_RATE
		 */

        Emulator.setValue(Emulator.SOUND_VALUE, prefsHelper.getSoundValue());
        Emulator.setValue(Emulator.SOUND_OPTIMAL_SAMPLERATE, sr);

		if(!mm.getPrefsHelper().getOverlayFilterValue().equals(PrefsHelper.PREF_OVERLAY_NONE))
		   Emulator.setValueStr(Emulator.OVERLAY_EFECT,mm.getPrefsHelper().getOverlayFilterValue());
    }

    public void updateMAME4droid() {

        if (Emulator.isRestartNeeded()) {
            mm.showDialog(DialogHelper.DIALOG_EMU_RESTART);
            return;
        }

        if (Emulator.isPortraitFull() != mm.getPrefsHelper().isPortraitFullscreen()) {
            mm.inflateViews();
        }

        View emuView = mm.getEmuView();

        InputView inputView = mm.getInputView();
        InputHandler inputHandler = mm.getInputHandler();
        PrefsHelper prefsHelper = mm.getPrefsHelper();

        String definedKeys = prefsHelper.getDefinedKeys();
        final String[] keys = definedKeys.split(":");
        for (int i = 0; i < keys.length; i++)
            GameController.keyMapping[i] = Integer.valueOf(keys[i]).intValue();

        Emulator.setDebug(prefsHelper.isDebugEnabled());

        updateEmuValues();

        if (prefsHelper.isTiltSensorEnabled())
            inputHandler.getTiltSensor().enable();
        else
            inputHandler.getTiltSensor().disable();

		int state = mm.getInputHandler().getTouchController().getState();

        if (this.getscrOrientation() == Configuration.ORIENTATION_PORTRAIT) {

            ((IEmuView) emuView).setScaleType(prefsHelper
                    .getPortraitScaleMode());

			Emulator.setEmuFiltering(prefsHelper.isPortraitBitmapFiltering());

            if (state == TouchController.STATE_SHOWING_CONTROLLER
                    && !prefsHelper.isPortraitTouchController())
                // {reload();return;}
                inputHandler.getTouchController().changeState();

            if (state == TouchController.STATE_SHOWING_NONE
                    && prefsHelper.isPortraitTouchController())
                // {reload();return;}
                inputHandler.getTouchController().changeState();

			int oldState = state;

            state = mm.getInputHandler().getTouchController().getState();

			if(oldState!=state && mm.getPrefsHelper().isPortraitFullscreen()) {//fix para cambio desde settings
				FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams) emuView.getLayoutParams();
				if(state == TouchController.STATE_SHOWING_CONTROLLER) {
					lp.gravity = Gravity.TOP | Gravity.CENTER;
				}
				else
				{
					lp.gravity = Gravity.CENTER;
				}
			}

            if (state == TouchController.STATE_SHOWING_NONE) {
                inputView.setVisibility(View.GONE);
            } else {
                inputView.setVisibility(View.VISIBLE);
            }

            if (state == TouchController.STATE_SHOWING_CONTROLLER) {
                if (Emulator.isPortraitFull()) {
                    inputView.bringToFront();
                    inputHandler.getTouchController()
                            .readControllerValues(R.raw.controller_portrait_full);
                } else {
                    inputView.setImageDrawable(mm.getResources().getDrawable(
                            R.drawable.back_portrait));
                    inputHandler.getTouchController()
                            .readControllerValues(R.raw.controller_portrait);
                }
            }

            if (ControlCustomizer.isEnabled() && !Emulator.isPortraitFull()) {
                ControlCustomizer.setEnabled(false);
                mm.getDialogHelper()
                        .setInfoMsg(
                                "Control layout customization is only allowed in fullscreen mode");
                mm.showDialog(DialogHelper.DIALOG_INFO);
            }
        } else {
            ((IEmuView) emuView).setScaleType(mm.getPrefsHelper()
                    .getLandscapeScaleMode());

			Emulator.setEmuFiltering(mm.getPrefsHelper()
				.isLandscapeBitmapFiltering());

			if (state == TouchController.STATE_SHOWING_CONTROLLER
                    && !prefsHelper.isLandscapeTouchController())
                // {reload();return;}
                inputHandler.getTouchController().changeState();

            if (state == TouchController.STATE_SHOWING_NONE
                    && prefsHelper.isLandscapeTouchController())
                // {reload();return;}
                inputHandler.getTouchController().changeState();

            state = mm.getInputHandler().getTouchController().getState();

            inputView.bringToFront();

            if (state == TouchController.STATE_SHOWING_NONE) {
                inputView.setVisibility(View.GONE);
            } else {
                inputView.setVisibility(View.VISIBLE);
            }

            if (state == TouchController.STATE_SHOWING_CONTROLLER) {
                inputView.setImageDrawable(null);

                Display dp = mm.getWindowManager().getDefaultDisplay();

                float w = dp.getWidth();
                float h = dp.getHeight();

                Point pt = new Point();
                dp.getRealSize(pt);
                w = pt.x;
                h = pt.y;

                if (h == 0)
                    h = 1;
/*
https://stackoverflow.com/questions/7199492/what-are-the-aspect-ratios-for-all-android-phone-and-tablet-devices

asus rog       --> 2160x1080 18:9
oneplus 6      --> 2280x1080 19:9   6.28
oneplus 7      --> 2340x1080 19.5:9 6.21
oneplus 8 	   --> 2400x1080 20.9   6.55
oneplus 8 pro  --> 3168x1440 19.8:9 6.78
galaxy sde	   --> 2560x1600 16:10


19.8:9 -> 2.2
20/9   -> 2,22222
19.5:9 -> 2,16666
19/9  -> 2,11111
18/9  -> 2
16/9   -> 1,7
5/3   -> 1,6666
4/3    -> 1,3
 */
                // System.out.println("--->>> "+w+" "+h+ " "+w/h+ " "+ (float)(16.0/9.0));


                float ar = w / h;
                if (ar >= (float) (18.0 / 9.0)) {
                    System.out.println("--->>> ULTRA WIDE");
                    inputHandler.getTouchController().readControllerValues(R.raw.controller_landscape_19_9);
                } else if (ar >= (float) (16.0 / 9.0) && ar < (float) (18.0 / 9.0)) {
                    System.out.println("--->>> WIDE");
                    inputHandler.getTouchController().readControllerValues(R.raw.controller_landscape_16_9);
                } else { //5 : 3
                    System.out.println("--->>> NORMAL");
                    inputHandler.getTouchController().readControllerValues(R.raw.controller_landscape);
                }

            }
        }

        oldInGame = Emulator.getValue(Emulator.IN_GAME);

        if (state != TouchController.STATE_SHOWING_CONTROLLER
                && ControlCustomizer.isEnabled()) {
            ControlCustomizer.setEnabled(false);
            mm.getDialogHelper()
                    .setInfoMsg(
                            "Control layout customization is only allowed when touch controller is visible");
            mm.showDialog(DialogHelper.DIALOG_INFO);
        }

        if (ControlCustomizer.isEnabled()) {
            // mm.getEmuView().setVisibility(View.INVISIBLE);
            // mm.getInputView().requestFocus();
        }
/*
        int op = mm.getMainHelper().getControllerAlpha();
        if (op != -1 && (state == TouchController.STATE_SHOWING_CONTROLLER))
            inputView.setAlpha(op);
*/
		inputView.requestLayout();
        emuView.requestLayout();

		inputView.invalidate();
        emuView.invalidate();


		//Log.d("isMouse"," value:"+mm.getPrefsHelper().isTouchMouse());
    }

    public void showSettings() {
        if (!Emulator.isEmulating()) return;
        Intent i = new Intent(mm, UserPreferences.class);
        mm.startActivityForResult(i, MainHelper.SUBACTIVITY_USER_PREFS);
    }

    public void showHelp() {
        // Intent i2 = new Intent(mm, HelpActivity.class);
        // mm.startActivityForResult(i2, MainHelper.SUBACTIVITY_HELP);
        if (mm.getMainHelper().isAndroidTV()) {
            mm.getDialogHelper()
                    .setInfoMsg(
                            "When MAME4droid is first run, it will create a folder structure for you on the internal memory of your Android device. This folder contains all the other folders MAME uses as well as some basic configuration files."
                                    + "Since MAME4droid does not come with game ROM files, you will need to copy them to (You can change it in options to external storage if your device allows it) '/storage/emulated/0/Android/media/com.seleuco.mame4d2024/roms' folder (" +
                                    "the one that applies) yourself. These should be properly named, ZIPped MAME v0.272 ROMs files with the filenames in all lower case.\n\nImportant: You should define or map your Android TV game controller on 'options/settings/input/External controller/define Keys' to avoid this help screen constantly showing if the controller is not auto detected.\n\n"
                                    + "Controls: Buttons A,B,C,D,E,F on the controller map to buttons Button MAME 1 to 6 buttons."
                                    + " Coin button inserts coin/adds credit.START button starts 1P game. A+START is fast forward. B+START is toggle UI controls. START+A+B is service mode. SELECT+A+B is soft reset. Use SELECT(coin) or START for UI navigation."
                                    + "R1 + START loads a save state. L1 + START saves a save state. START + SELECT when gaming accesses the game's MAME menu (dip switches, etc)...");
            mm.showDialog(DialogHelper.DIALOG_INFO);
        } else {

            Intent i = new Intent(mm, WebHelpActivity.class);
            i.putExtra("INSTALLATION_PATH", mm.getMainHelper()
                    .getInstallationDIR());
            try {
                mm.startActivityForResult(i, MainHelper.SUBACTIVITY_HELP);
            } catch (RuntimeException e) {
                e.printStackTrace();
            }
			/*
			mm.getDialogHelper()
			.setInfoMsg(
					"When MAME4droid is first run, it will create a folder structure for you on the internal memory of your Android device. This folder contains all the other folders MAME uses as well as some basic configuration files."
							+ "Since MAME4droid does not come with game ROM files, you will need to copy them to the '/sdcard/MAME4droid/roms' or 'Android/data/com.seleuco.mame4droid/files/roms' folder (" +
							"the one that applies) yourself. These should be properly named, ZIPped MAME v1.39u1 ROMs files with the filenames in all lower case.\n\n"
							+ "Controls: Buttons A,B,C,D,E,F on the controller map to buttons Button MAME 1 to 6 buttons."
							+ "Coin button inserts coin/adds credit.START button starts 1P game.START+UP starts 2P game. START+RIGHT starts 3P game. START+DOWN starts 4P game.SELECT+UP inserts 2P credits. SELECT+RIGHT inserts 3P credits. SELECT+DOWN inserts 4P credits."
							+ "R1 + START loads a save state. L1 + START saves a save state. START + SELECT when gaming accesses the game's MAME menu (dip switches, etc)...");
	         mm.showDialog(DialogHelper.DIALOG_INFO);
	         */
        }
    }


    public void activityResult(int requestCode, int resultCode, Intent intent) {

        if (requestCode == SUBACTIVITY_USER_PREFS) {
            updateMAME4droid();
        }

        if (requestCode == MainHelper.REQUEST_CODE_OPEN_DIRECTORY &&
                resultCode == Activity.RESULT_OK && intent != null) {
            final ContentResolver resolver = mm.getContentResolver();
            final Uri uri = intent.getData();
            final int takeFlags = Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
            resolver.takePersistableUriPermission(uri, takeFlags);
            final Uri dirUri = DocumentsContract.buildDocumentUriUsingTree(uri, DocumentsContract.getTreeDocumentId(uri));

            mm.getSAFHelper().setURI(dirUri.toString());

            System.out.println("SAF ROMS dirUri:" + dirUri.getPath());

            String romsPath = mm.getSAFHelper().pathFromDocumentUri(uri);
            if (romsPath == null)
                romsPath = "/Your_Selected_Folder";

            mm.getMainHelper().setInstallationDirType(MainHelper.INSTALLATION_DIR_FILES_DIR);
            mm.getPrefsHelper().setROMsDIR(romsPath);
            mm.getPrefsHelper().setSAF_Uri(uri.toString());

			Thread t = new Thread(new Runnable() { public void run() {
				try {
					Thread.sleep(250);
				} catch (InterruptedException e) {
					throw new RuntimeException(e);
				}
				mm.runMAME4droid();
			}});
			t.start();

			//mm.runMAME4droid();
        }
    }

    public ArrayList<Integer> measureWindow(int widthMeasureSpec,
                                            int heightMeasureSpec, int scaleType) {

        int widthSize = 1;
        int heightSize = 1;

        if (!Emulator.isInGame() && !(scaleType == PrefsHelper.PREF_STRETCH))
            scaleType = PrefsHelper.PREF_SCALE;

        if (scaleType == PrefsHelper.PREF_STRETCH)// FILL ALL
        {
            widthSize = MeasureSpec.getSize(widthMeasureSpec);
            heightSize = MeasureSpec.getSize(heightMeasureSpec);
        } else {
            int emu_w = Emulator.getEmulatedVisWidth();
            int emu_h = Emulator.getEmulatedVisHeight();

            if (scaleType == PrefsHelper.PREF_SCALE_INTEGER) {

                int ax = (MeasureSpec.getSize(widthMeasureSpec) / emu_w);
                int ay = (MeasureSpec.getSize(heightMeasureSpec) / emu_h);

                int xx = Math.min(ax, ay);

                if (xx == 0)
                    xx = 1;

                emu_w = emu_w * xx;
                emu_h = emu_h * xx;
            } else if (scaleType == PrefsHelper.PREF_SCALE_INTEGER_BEYOND) {

                int ax = (MeasureSpec.getSize(widthMeasureSpec) / emu_w);
                int ay = (MeasureSpec.getSize(heightMeasureSpec) / emu_h);

                ax++;
                ay++;
                int xx = Math.min(ax, ay);

                if (xx == 0)
                    xx = 1;

                emu_w = emu_w * xx;
                emu_h = emu_h * xx;
            } else if (scaleType == PrefsHelper.PREF_15X) {
                emu_w = (int) (emu_w * 1.5f);
                emu_h = (int) (emu_h * 1.5f);
            } else if (scaleType == PrefsHelper.PREF_20X) {
                emu_w = emu_w * 2;
                emu_h = emu_h * 2;
            }

            int w = emu_w;
            int h = emu_h;

            if (scaleType == PrefsHelper.PREF_SCALE
                    || scaleType == PrefsHelper.PREF_STRETCH
                    || !Emulator.isInGame()
                    || !mm.getPrefsHelper().isScaleBeyondBoundaries()) {
                widthSize = MeasureSpec.getSize(widthMeasureSpec);
                heightSize = MeasureSpec.getSize(heightMeasureSpec);

                if (mm.getPrefsHelper().isOverscan()) {
                    widthSize *= 0.93;
                    heightSize *= 0.93;
                }

				if(mm.getPrefsHelper().isZoomTo40() && Emulator.isInGame()){
					widthSize *= 1.35;
					heightSize *= 1.35;
				}

            } else {
                widthSize = emu_w;
                heightSize = emu_h;
            }

            if (heightSize == 0)
                heightSize = 1;
            if (widthSize == 0)
                widthSize = 1;

            float scale = 1.0f;

            if (scaleType == PrefsHelper.PREF_SCALE)
                scale = Math.min((float) widthSize / (float) w,
                        (float) heightSize / (float) h);

           w = (int) (w * scale);
           h = (int) (h * scale);

           widthSize = Math.min(w, widthSize);
           heightSize = Math.min(h, heightSize);

            if (heightSize == 0)
                heightSize = 1;
            if (widthSize == 0)
                widthSize = 1;

			Log.d("onMeasure", "scale: "+scale +" emu_width:"+emu_w+" emu_height: "+emu_h+" newWidth:"+w+" newHeight: "+h);

			if( mm.getPrefsHelper().getEmulatedResolution()==0 /*auto*/) {

				float desiredAspect = (float) emu_w / (float) emu_h;

				float actualAspect = (float)widthSize / (float)heightSize;

				if (Math.abs(actualAspect - desiredAspect) > 0.0000001) {

					boolean done = false;

					// Try adjusting emu_width to be proportional to emu_height
					int newWidth = (int) (desiredAspect * heightSize);

					if (newWidth <= widthSize) {
						widthSize = newWidth;
						done = true;
					}

					// Try adjusting emu_height to be proportional to emu_width
					if (!done) {
						int newHeight = (int) (widthSize / desiredAspect);
						if (newHeight <= heightSize) {
							heightSize = newHeight;
						}
					}
				}

			}
        }

        ArrayList<Integer> l = new ArrayList<Integer>();
        l.add(Integer.valueOf(widthSize));
        l.add(Integer.valueOf(heightSize));
        return l;
    }

    public void detectDevice() {

		Log.d("model",android.os.Build.MODEL);

        boolean shield = android.os.Build.MODEL.equals("SHIELD");
		boolean quest = android.os.Build.MODEL.toUpperCase().equals("QUEST");

		if(quest) {



			Context context = mm.getApplicationContext();
			SharedPreferences prefs = PreferenceManager
				.getDefaultSharedPreferences(context);
			if (!prefs.getBoolean("metaquest", false)) {
				SharedPreferences.Editor edit = prefs.edit();
				edit.putBoolean("metaquest", true);
				//edit.putBoolean(PrefsHelper.PREF_LANDSCAPE_TOUCH_CONTROLLER, false);

				edit.putBoolean(PrefsHelper.PREF_HIDE_STICK, true);

				edit.putBoolean(PrefsHelper.PREF_LANDSCAPE_BITMAP_FILTERING,
					true);

				edit.putBoolean(PrefsHelper.PREF_TOUCH_GAME_MOUSE,
					false);

				edit.putString(PrefsHelper.PREF_EMU_RESOLUTION, "2");
				edit.putString(PrefsHelper.PREF_EMU_RESOLUTION_OSD, "2");

				edit.putString(PrefsHelper.PREF_ORIENTATION, "2");

				edit.putString(PrefsHelper.PREF_NUMBUTTONS, "1");

				// edit.putString("", "");
				edit.commit();

			}
			deviceDetected = DEVICE_METAQUEST;
		}
        else if (shield) {
            Context context = mm.getApplicationContext();
            SharedPreferences prefs = PreferenceManager
                    .getDefaultSharedPreferences(context);
            if (!prefs.getBoolean("shield_3", false)) {
                SharedPreferences.Editor edit = prefs.edit();
                edit.putBoolean("shield_3", true);
                edit.putBoolean(PrefsHelper.PREF_LANDSCAPE_TOUCH_CONTROLLER,
                        false);
                edit.putString(PrefsHelper.PREF_GLOBAL_NAVBAR_MODE,
                        PrefsHelper.PREF_NAVBAR_VISIBLE + "");
                edit.putBoolean(PrefsHelper.PREF_LANDSCAPE_BITMAP_FILTERING,
                        true);
                edit.putString(PrefsHelper.PREF_EMU_RESOLUTION, "4");
                edit.commit();
            }
            deviceDetected = DEVICE_SHIELD;

        }else if (isAndroidTV()) {
            Context context = mm.getApplicationContext();
            SharedPreferences prefs = PreferenceManager
                    .getDefaultSharedPreferences(context);
            if (!prefs.getBoolean("androidtv", false)) {
                SharedPreferences.Editor edit = prefs.edit();
                edit.putBoolean("androidtv", true);
                edit.putBoolean(PrefsHelper.PREF_LANDSCAPE_TOUCH_CONTROLLER,
                        false);
                edit.putBoolean(PrefsHelper.PREF_LANDSCAPE_BITMAP_FILTERING,
                        true);

                edit.putString(PrefsHelper.PREF_EMU_RESOLUTION, "1");
				edit.putString(PrefsHelper.PREF_EMU_RESOLUTION_OSD, "4");

				//edit.putString(PrefsHelper.PREF_ORIENTATION, "4");

				// edit.putString("", "");
                edit.commit();
            }
            deviceDetected = DEVICE_ANDROIDTV;
        }
    }

    public void restartApp() {

        if (Build.VERSION.SDK_INT < 30) {
            Intent oldintent = mm.getIntent();
            // System.out.println("OLD INTENT:"+oldintent.getAction());
            int flags = oldintent.getFlags();

            if(Build.VERSION.SDK_INT >= 33)//para que no saque error el UI
               flags |=  PendingIntent.FLAG_IMMUTABLE;//67108864; //FLAG_IMMUTABLE

            PendingIntent intent = PendingIntent.getActivity(mm.getBaseContext(),
                    0, new Intent(oldintent), flags);
            AlarmManager manager = (AlarmManager) mm
                    .getSystemService(Context.ALARM_SERVICE);
            manager.set(AlarmManager.RTC, System.currentTimeMillis() + 250, intent);
        }
        android.os.Process.killProcess(android.os.Process.myPid());
    }

    public void checkNewViewIntent(Intent intent) {//TODO
        if (Intent.ACTION_VIEW.equals(intent.getAction()) && Emulator.isEmulating()) {
            Uri uri = intent.getData();
            java.io.File f = new java.io.File(uri.getPath());
            String name = f.getName();
            String romName = Emulator.getValueStr(Emulator.ROM_NAME);
            // System.out.print("Intent view: "+name + " "+ romName);
            if (/*romName != null && */name.equals(romName))
                return;
            mm.setIntent(intent);
            new Thread(new Runnable() {
                public void run() {
                    try {
                        Thread.sleep(500);
                    } catch (InterruptedException e) {
                        e.printStackTrace();
                    }
                    restartApp();
                }
            }).start();
        }
    }

    public String getVersion() {
        String version = "???";
        try {
            version = mm.getPackageManager().getPackageInfo(
                    mm.getPackageName(), 0).versionName;
        } catch (NameNotFoundException e) {
            e.printStackTrace();
        }
        return version;
    }

    public boolean isAndroidTV() {
        try {

            android.app.UiModeManager uiModeManager = (android.app.UiModeManager) mm
                    .getSystemService(Context.UI_MODE_SERVICE);
            if (uiModeManager.getCurrentModeType() == Configuration.UI_MODE_TYPE_TELEVISION)
                return true;
            else
                return false;
        } catch (Throwable e) {
            return false;
        }
    }

    //@SuppressLint("Range")
    public String getFileName(Uri uri) {
        String result = null;
        if ("content".equals(uri.getScheme())) {
            Cursor cursor = mm.getContentResolver().query(uri, null, null, null, null);
            try {
                if (cursor != null && cursor.moveToFirst()) {
                    int i = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                    result = cursor.getString(i);
                }
            } finally {
                if (cursor != null)
                    cursor.close();
            }
        }
        if (result == null) {
            result = uri.getPath();
            if (result != null) {
                int cut = result.lastIndexOf('/');
                if (cut != -1) {
                    result = result.substring(cut + 1);
                }
            }
        }
        return result;
    }

    public boolean copyFile(InputStream input, String path, String fileName) {
        boolean error = false;
        try {
            File file = new File(path, fileName);
            try (OutputStream output = new FileOutputStream(file)) {
                byte[] buffer = new byte[4 * 1024];
                int read;
                while ((read = input.read(buffer)) != -1) {
                    output.write(buffer, 0, read);
                }
                output.flush();
            }
        } catch (Exception e) {
            error = true;
        } finally {
            try {
                input.close();
            } catch (IOException e) {
                e.printStackTrace();
            }
        }
        return error;
    }

	public Size getWindowSize(){
		Size size;

		if(Build.VERSION.SDK_INT < 30) {
			Display display = mm.getWindowManager().getDefaultDisplay();
			Point s = new Point();
			display.getSize(s);
			int width = s.x;
			int height = s.y;
			size= new Size(width,height);
		}
		else {
			final WindowMetrics metrics = mm.getWindowManager().getCurrentWindowMetrics();
			// Gets all excluding insets
			final WindowInsets windowInsets = metrics.getWindowInsets();

			final Rect bounds = metrics.getBounds();
			size = new Size(bounds.width(), bounds.height());


			if (!mm.getPrefsHelper().isNotchUsed()) {

				Insets insets = windowInsets.getInsetsIgnoringVisibility(
					//WindowInsets.Type.navigationBars()|
					WindowInsets.Type.displayCutout());

				int insetsWidth = insets.right + insets.left;
				int insetsHeight = insets.top + insets.bottom;

				size = new Size(size.getWidth() - insetsWidth, size.getHeight() - insetsHeight);
			}
		}

		Log.d("SIZE","Window size is width:"+size.getWidth()+" height:"+size.getHeight());

		return size;
	}

	public int getScreenOrientation(){
		int orientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR;
		switch (mm.getPrefsHelper().getOrientationMode()){
			case 1 : orientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE;break;
			case 2 : orientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE;break;
			case 3 : orientation = ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE;break;
			case 4 : orientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT;break;
			case 5 : orientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT;break;
			case 6 : orientation = ActivityInfo.SCREEN_ORIENTATION_REVERSE_PORTRAIT;break;
		}
		return orientation;
	}

	public ArrayList<ArrayList<String>> readShaderCfg(String path){
		ArrayList<ArrayList<String>> data = new ArrayList<>();

		if(path!=null) {

			try(BufferedReader br = new BufferedReader(new FileReader(path+"/shaders.cfg"))) {
				StringBuilder sb = new StringBuilder();
				String line = br.readLine();

				while (line != null) {
					sb.append(line);
					sb.append(System.lineSeparator());
					line = br.readLine();
					if(line!=null) {
						line = line.trim();
						if (line.length()==0)
							continue;
						if (line.startsWith("#"))
							continue;
						ArrayList<String> a = new ArrayList<>();
						StringTokenizer tokens = new StringTokenizer(line, ";");
						while (tokens.hasMoreTokens()) {
							a.add(tokens.nextToken());
						}
						data.add(a);
					}
				}
			} catch (IOException e) {
			}
		}

		return data;
	}

	public int getControllerAlpha() {
		int alpha=0;

		if (this.getscrOrientation() == Configuration.ORIENTATION_PORTRAIT && !mm.getPrefsHelper().isPortraitFullscreen() )
		    alpha = 255;
		else
			alpha = (255 * mm.getPrefsHelper().getButtonsAlpha())/100;

		return alpha;
	}

}
