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

package com.seleuco.mame4droid.prefs;

import android.app.ListActivity;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.ArrayAdapter;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;

import com.seleuco.mame4droid.input.GameController;

public class ListKeys extends ListActivity {

	public static final String[] androidKeysLabels = {
		/* 0 - 9*/
		"UNKNOWN", "SOFT_LEFT","SOFT_RIGHT", "HOME", "BACK", "CALL", "ENDCALL", "0", "1", "2",
		/* 10 - 19*/
		"3", "4", "5", "6", "7", "8", "9", "STAR", "POUND", "DPAD_UP",
		/* 20 - 29*/
		"DPAD_DOWN", "DPAD_LEFT", "DPAD_RIGHT", "DPAD_CENTER", "VOLUME_UP","VOLUME_DOWN", "POWER", "CAMERA", "CLEAR", "A",
		/* 30 - 39*/
		"B", "C", "D", "E","F", "G", "H", "I", "J", "K",
		/* 40 - 49*/
		"L", "M", "N", "O", "P", "Q", "R","S", "T", "U",
		/* 50 - 59*/
		"V", "W", "X", "Y", "Z", "COMMA", "PERIOD","ALT_LEFT", "ALT_RIGHT", "SHIFT_LEFT",
		/* 60 - 69*/
		"SHIFT_RIGHT", "TAB","SPACE", "SYM", "EXPLORER", "ENVELOPE", "ENTER", "DEL", "GRAVE","MINUS",
		/* 70 - 79*/
		"EQUALS", "LEFT_BRACKET", "RIGHT_BRACKET", "BACKSLASH","SEMICOLON", "APOSTROPHE", "SLASH", "AT", "NUM", "HEADSETHOOK",
		/* 80 - 89*/
		"FOCUS", "PLUS", "MENU", "NOTIFICATION", "SEARCH","MEDIA_PLAY_PAUSE", "MEDIA_STOP", "MEDIA_NEXT", "MEDIA_PREVIOUS","MEDIA_REWIND",
		/* 90 - 99*/
		"MEDIA_FAST_FORWARD", "MUTE","PAGE_UP","PAGE_DOWN","PICTSYMBOLS","SWITCH_CHARSET" ,"BUTTON_A","BUTTON_B","BUTTON_C","BUTTON_X",
		/* 100 - 109*/
		"BUTTON_Y" ,"BUTTON_Z","BUTTON_L1","BUTTON_R1","BUTTON_L2" ,"BUTTON_R2","BUTTON_THUMBL","BUTTON_THUMBR","BUTTON_START","BUTTON_SELECT",
		/* 110 - 119*/
		"BUTTON_MODE","ESCAPE","FORWARD_DEL","CTRL_LEFT","CTRL_RIGHT","CAPS_LOCK","SCROLL_LOCK","META_LEFT","META_RIGHT","FUNCTION",
		/* 120 - 129*/
		"SYSRQ","BREAK","MOVE_HOME","MOVE_END","INSERT","FORWARD","MEDIA_PLAY","MEDIA_PAUSE","MEDIA_CLOSE","MEDIA_EJECT",
		/* 130 - 139*/
		"MEDIA_RECORD","F1","F2","F3","F4","F5","F6","F7","F8","F9",
		/* 140 - 149*/
		"F10","F11","F12","NUM_LOCK","NUMPAD_0","NUMPAD_1","NUMPAD_2","NUMPAD_3","NUMPAD_4","NUMPAD_5",
		/* 150 - 159*/
		"NUMPAD_6","NUMPAD_7","NUMPAD_8","NUMPAD_9","NUMPAD_DIVIDE","NUMPAD_MULTIPLY","NUMPAD_SUBTRACT","NUMPAD_ADD","NUMPAD_DOT","NUMPAD_COMMA",
		/* 160 - 169*/
		"NUMPAD_ENTER","NUMPAD_EQUALS","NUMPAD_LEFT_PAREN","NUMPAD_RIGHT_PAREN","VOLUME_MUTE","INFO","CHANNEL_UP","CHANNEL_DOWN","ZOOM_IN","ZOOM_OUT",
		/* 170 - 179*/
		"TV","WINDOW","GUIDE","DVR","BOOKMARK","CAPTIONS","SETTINGS","TV_POWER","TV_INPUT","STB_POWER",
		/* 180 - 189*/
		"STB_INPUT","AVR_POWER","AVR_INPUT","PROG_RED","PROG_GREEN","PROG_YELLOW","PROG_BLUE","APP_SWITCH","BUTTON_1","BUTTON_2",
		/* 190 - 199*/
		"BUTTON_3","BUTTON_4","BUTTON_5","BUTTON_6","BUTTON_7","BUTTON_8","BUTTON_9","BUTTON_10","BUTTON_11","BUTTON_12",
		/* 200 - 204*/
		"BUTTON_13","BUTTON_14","BUTTON_15","BUTTON_16"
	};

	public static final String[] emulatorInputLabels = {
		"Up", "Down", "Left", "Right",
		"Button A", "Button B", "Button C", "Button D",
		"Button E", "Button F", "Button G", "Button H",
		"Coin", "Start", "Exit", "Option",
	};

	protected int emulatorInputIndex = 0;
	protected int controllerIndex = 0;

	private ArrayAdapter<String> keyLabelsAdapter;

	@Override
	public void onCreate(Bundle icicle) {
		super.onCreate(icicle);

		getWindow().setFlags(WindowManager.LayoutParams.FLAG_BLUR_BEHIND,
			WindowManager.LayoutParams.FLAG_BLUR_BEHIND);

		controllerIndex = getIntent().getIntExtra("controllerIndex", 0);
		setTitle("MAME4droid Controller " + (controllerIndex + 1) + " buttons");

		drawListAdapter();
	}

	private void drawListAdapter() {
		if (keyLabelsAdapter == null) {
			final Context context = this;

			keyLabelsAdapter = new ArrayAdapter<String>(this, 0, emulatorInputLabels) {
				@Override
				public View getView(int position, View convertView, ViewGroup parent) {
					LinearLayout layout;
					TextView leftText;
					TextView rightText;

					if (convertView == null) {
						layout = new LinearLayout(context);
						layout.setOrientation(LinearLayout.HORIZONTAL);
						layout.setGravity(Gravity.CENTER_VERTICAL);

						int paddingHorizontal = (int) (16 * getResources().getDisplayMetrics().density);
						int minHeight = (int) (56 * getResources().getDisplayMetrics().density);

						layout.setPadding(paddingHorizontal, 0, paddingHorizontal, 0);
						layout.setMinimumHeight(minHeight);

						// Text("Button A")
						leftText = new TextView(context);
						leftText.setId(android.R.id.text1);
						leftText.setTextAppearance(context, android.R.style.TextAppearance_Medium);
						leftText.setLayoutParams(new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.0f));

						// Text ("ID 9999 : BUTTON_A")
						rightText = new TextView(context);
						rightText.setId(android.R.id.text2);
						rightText.setTextAppearance(context, android.R.style.TextAppearance_Small);
						rightText.setGravity(Gravity.END | Gravity.CENTER_VERTICAL);
						rightText.setLayoutParams(new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));

						layout.addView(leftText);
						layout.addView(rightText);
					} else {
						layout = (LinearLayout) convertView;
						leftText = layout.findViewById(android.R.id.text1);
						rightText = layout.findViewById(android.R.id.text2);
					}

					int mapPosition = (controllerIndex * emulatorInputLabels.length) + position;
					leftText.setText(getItem(position));

					int mappedValue = GameController.keyMapping[mapPosition];

					if (mappedValue != -1) {
						String label = "???";
						int idx = GameController.getKeyCodeFromKeyCodeWithDeviceID(mappedValue);

						if (idx < androidKeysLabels.length) {
							label = androidKeysLabels[idx];
						}

						rightText.setText(String.format("ID %d : %s",
							GameController.getDeviceIdFromKeyCodeWithDeviceID(mappedValue),
							label
						));
					} else {
						rightText.setText("Not mapped");
					}

					return layout;
				}
			};

			setListAdapter(keyLabelsAdapter);
		} else {
			keyLabelsAdapter.notifyDataSetChanged();
		}
	}

	@Override
	public void onListItemClick(ListView parent, View v, int position, long id) {
		emulatorInputIndex = position;
		startActivityForResult(new Intent(this, KeySelect.class).putExtra(
			"emulatorInputIndex", emulatorInputIndex), 0);
	}

	protected void onActivityResult(int requestCode, int resultCode, Intent data) {
		super.onActivityResult(requestCode, resultCode, data);

		if (resultCode == RESULT_OK && requestCode == 0) {
			int androidKeyCode = data.getIntExtra("androidKeyCode", 0);
			int androidGamePadID = data.getIntExtra("androidGamePadID", 0);
			int iKeyCodeWithDeviceId = GameController.makeKeyCodeWithDeviceID(androidGamePadID, androidKeyCode);

			for (int i = 0; i < GameController.keyMapping.length; i++) {
				if (GameController.keyMapping[i] == iKeyCodeWithDeviceId)
					GameController.keyMapping[i] = -1;
			}

			GameController.keyMapping[(controllerIndex * emulatorInputLabels.length) + emulatorInputIndex] = iKeyCodeWithDeviceId;
		}

		drawListAdapter();
	}
}
