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

import android.app.Activity;
import android.app.Dialog;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.graphics.Insets;
import android.graphics.Rect;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.DisplayCutout;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.widget.FrameLayout;

import com.seleuco.mame4droid.helpers.DialogHelper;
import com.seleuco.mame4droid.helpers.MainHelper;
import com.seleuco.mame4droid.helpers.PrefsHelper;
import com.seleuco.mame4droid.helpers.SAFHelper;
import com.seleuco.mame4droid.helpers.ScraperHelper;
import com.seleuco.mame4droid.input.ControlCustomizer;
import com.seleuco.mame4droid.input.GameController;
import com.seleuco.mame4droid.input.InputHandler;
import com.seleuco.mame4droid.views.IEmuView;
import com.seleuco.mame4droid.views.InputView;

import java.util.List;

public class MAME4droid extends Activity {

	protected View emuView = null;

	protected InputView inputView = null;

	protected MainHelper mainHelper = null;
	protected PrefsHelper prefsHelper = null;
	protected DialogHelper dialogHelper = null;
	protected SAFHelper safHelper = null;
	protected ScraperHelper scraperHelper = null;

	protected InputHandler inputHandler = null;

	public PrefsHelper getPrefsHelper() {
		return prefsHelper;
	}

	public MainHelper getMainHelper() {
		return mainHelper;
	}

	public DialogHelper getDialogHelper() {
		return dialogHelper;
	}

	public SAFHelper getSAFHelper() {
		return safHelper;
	}

	public ScraperHelper getScraperHelper() {
		return scraperHelper;
	}

	public View getEmuView() {
		return emuView;
	}

	public InputView getInputView() {
		return inputView;
	}

	public InputHandler getInputHandler() {
		return inputHandler;
	}

	/**
	 * Called when the activity is first created.
	 */
	@Override
	public void onCreate(Bundle savedInstanceState) {

		super.onCreate(savedInstanceState);

		//android.os.Debug.waitForDebugger();

		Log.d("EMULATOR", "onCreate " + this);
		System.out.println("onCreate intent:" + getIntent().getAction());

		overridePendingTransition(0, 0);
		getWindow().setWindowAnimations(0);

		prefsHelper = new PrefsHelper(this);

		dialogHelper = new DialogHelper(this);

		mainHelper = new MainHelper(this);

		safHelper = new SAFHelper(this);

		scraperHelper = new ScraperHelper(this);

		inputHandler = new InputHandler(this);

		mainHelper.detectDevice();

		inflateViews();

		Emulator.setMAME4droid(this);

		mainHelper.updateMAME4droid();

		String uri = getPrefsHelper().getSAF_Uri();
		if (uri != null) {
			safHelper.setURI(uri);
		}

		initMame4droid();
	}

	protected void initMame4droid() {
		if (!Emulator.isEmulating()) {

			if (getPrefsHelper().getInstallationDIR() == null || prefsHelper.getROMsDIR() == null) {
				if (DialogHelper.savedDialog == DialogHelper.DIALOG_NONE) {
					if(getMainHelper().isAndroidTV() && getPrefsHelper().getInstallationDIR() == null ) {
						getMainHelper().setInstallationDirType(MainHelper.INSTALLATION_DIR_MEDIA_FOLDER);
						getPrefsHelper().setROMsDIR("");
						getPrefsHelper().setSAF_Uri(null);
						Thread t = new Thread(new Runnable() { public void run() { runMAME4droid();
						}});
						t.start();
					}
					else
					   showDialog(DialogHelper.DIALOG_ROMs);
				}
			} else { //roms dir no es null es que previamente hemos puesto "" o un path especifico. Venimos del recreate y si ha cambiado el installation path hay que actuzalizarlo

				boolean res = getMainHelper().ensureInstallationDIR(mainHelper.getInstallationDIR());
				if (!res) {
					this.getPrefsHelper().setInstallationDIR(this.getPrefsHelper().getOldInstallationDIR());//revert
				} else {
					runMAME4droid();//MAIN ENTRY POINT
				}
			}
		}
	}


	public void inflateViews() {

		if (getPrefsHelper().getOrientationMode() != 0) {
			int mode = getMainHelper().getScreenOrientation();
			this.setRequestedOrientation(mode);
		}

		if (getPrefsHelper().isNotchUsed() && Build.VERSION.SDK_INT < Build.VERSION_CODES.VANILLA_ICE_CREAM) {
			getWindow().getAttributes().layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
		}

		inputHandler.unsetInputListeners();

		Emulator.setPortraitFull(getPrefsHelper().isPortraitFullscreen());

		boolean full = false;
		if (prefsHelper.isPortraitFullscreen() && mainHelper.getscrOrientation() == Configuration.ORIENTATION_PORTRAIT) {
			setContentView(R.layout.main_fullscreen);
			full = true;
		} else {
			setContentView(R.layout.main);
		}

		FrameLayout fl = (FrameLayout) this.findViewById(R.id.EmulatorFrame);

		if (prefsHelper.getNavBarMode() != PrefsHelper.PREF_NAVBAR_VISIBLE)
			this.getLayoutInflater().inflate(R.layout.emuview_gl_ext, fl);
		else
			this.getLayoutInflater().inflate(R.layout.emuview_gl, fl);

		emuView = this.findViewById(R.id.EmulatorViewGL);

		if (full && prefsHelper.isPortraitTouchController()
		) {
			FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams) emuView.getLayoutParams();
			lp.gravity = Gravity.TOP | Gravity.CENTER;
		}

		inputView = (InputView) this.findViewById(R.id.InputView);

		((IEmuView) emuView).setMAME4droid(this);

		inputView.setMAME4droid(this);

		View frame = this.findViewById(R.id.EmulatorFrame);
		frame.setOnTouchListener(inputHandler);

		if (!getPrefsHelper().isNotchUsed() &&  Build.VERSION.SDK_INT >= Build.VERSION_CODES.VANILLA_ICE_CREAM) {
			frame.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener() {
				@Override
				public WindowInsets onApplyWindowInsets(View v, WindowInsets insets) {
					 Insets bars = insets.getInsets(WindowInsets.Type.displayCutout()

					/*	 | WindowInsets.Type.systemBars() */

					 );
					v.setPadding(bars.left, bars.top, bars.right, bars.bottom);
					return WindowInsets.CONSUMED;
				}
			});
		}

		inputHandler.setInputListeners();
	}

	public void runMAME4droid() {

		getMainHelper().copyFiles();
		getMainHelper().removeFiles();

		Emulator.emulate(mainHelper.getLibDir(), mainHelper.getInstallationDIR());
	}


	@Override
	public void onConfigurationChanged(Configuration newConfig) {
		Log.d("EMULATOR", "onConfigurationChanged " + this);

		super.onConfigurationChanged(newConfig);

		overridePendingTransition(0, 0);

		inflateViews();

		getMainHelper().updateMAME4droid();

		overridePendingTransition(0, 0);
	}


	//ACTIVITY
	@Override
	protected void onActivityResult(int requestCode, int resultCode, Intent data) {
		super.onActivityResult(requestCode, resultCode, data);
		if (mainHelper != null)
			mainHelper.activityResult(requestCode, resultCode, data);
	}

	//LIVE CYCLE
	@Override
	protected void onResume() {
		Log.d("EMULATOR", "onResume " + this);
		super.onResume();

		if (prefsHelper != null)
			prefsHelper.resume();

		if (DialogHelper.savedDialog != -1)
			showDialog(DialogHelper.savedDialog);
		else if (!ControlCustomizer.isEnabled())
			Emulator.resume();

		if (inputHandler != null) {
			if (inputHandler.getTiltSensor() != null)
				inputHandler.getTiltSensor().enable();
		}

		if(scraperHelper!=null){
			scraperHelper.resume();
		}

		//System.out.println("OnResume");
	}

	@Override
	protected void onPause() {
		Log.d("EMULATOR", "onPause " + this);
		super.onPause();
		if (prefsHelper != null)
			prefsHelper.pause();
		if (!ControlCustomizer.isEnabled())
			Emulator.pause();
		if (inputHandler != null) {
			if (inputHandler.getTiltSensor() != null)
				inputHandler.getTiltSensor().disable();
		}

		if (dialogHelper != null) {
			dialogHelper.removeDialogs();
		}
		if(scraperHelper!=null){
			scraperHelper.pause();
		}

		//System.out.println("OnPause");
	}

	@Override
	protected void onStart() {
		Log.d("EMULATOR", "onStart " + this);
		super.onStart();
		try {
			GameController.resetAutodetected();
		} catch (Throwable ignored) {
		}
		//System.out.println("OnStart");
	}

	@Override
	protected void onStop() {
		Log.d("EMULATOR", "onStop " + this);
		super.onStop();
		//System.out.println("OnStop");
	}

	@Override
	protected void onNewIntent(Intent intent) {
		super.onNewIntent(intent);
		Log.d("EMULATOR", "onNewIntent " + this);
		System.out.println("onNewIntent action:" + intent.getAction());
		mainHelper.checkNewViewIntent(intent);
	}

	@Override
	protected void onDestroy() {
		super.onDestroy();
		Log.d("EMULATOR", "onDestroy " + this);

		View frame = this.findViewById(R.id.EmulatorFrame);
		if (frame != null)
			frame.setOnTouchListener(null);

		if (inputHandler != null) {
			inputHandler.unsetInputListeners();

			if (inputHandler.getTiltSensor() != null)
				inputHandler.getTiltSensor().disable();
		}

		if (emuView != null)
			((IEmuView) emuView).setMAME4droid(null);

		if(scraperHelper!=null){
			scraperHelper.stop();
		}

        /*
        if(inputView!=null)
           inputView.setMAME4droid(null);

        if(filterView!=null)
           filterView.setMAME4droid(null);

        prefsHelper = null;

        dialogHelper = null;

        mainHelper = null;

        inputHandler = null;

        inputView = null;

        emuView = null;

        filterView = null; */
	}


	//Dialog Stuff
	@Override
	protected Dialog onCreateDialog(int id) {

		if (dialogHelper != null) {
			Dialog d = dialogHelper.createDialog(id);
			if (d != null) return d;
		}
		return super.onCreateDialog(id);
	}

	@Override
	protected void onPrepareDialog(int id, Dialog dialog) {
		if (dialogHelper != null)
			dialogHelper.prepareDialog(id, dialog);
	}

	@Override
	public boolean dispatchGenericMotionEvent(MotionEvent event) {
		if (inputHandler != null)
			return inputHandler.genericMotion(event);
		return false;
	}

	@Override
	public void onPointerCaptureChanged(boolean hasCapture) {
		super.onPointerCaptureChanged(hasCapture);
	}

	@Override
	public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
		switch (requestCode) {
			case 1:
				if (grantResults == null || grantResults.length == 0) {
					//this.showDialog(DialogHelper.DIALOG_NO_PERMISSIONS);
					System.out.println("***1");
				} else if (grantResults[0] == PackageManager.PERMISSION_GRANTED) {
					System.out.println("***2");
					initMame4droid();
				} else {
					System.out.println("***3");
					this.showDialog(DialogHelper.DIALOG_NO_PERMISSIONS);
				}
				break;
			default:
				super.onRequestPermissionsResult(requestCode, permissions, grantResults);
		}
	}

}
