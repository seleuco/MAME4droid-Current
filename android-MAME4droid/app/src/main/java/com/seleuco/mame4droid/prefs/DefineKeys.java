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
import android.content.Intent;
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager;
import android.widget.ArrayAdapter;
import android.widget.ListView;

public class DefineKeys extends ListActivity {

    protected int controllerIndex = 0;

	private ArrayAdapter<String> keyLabelsAdapter;

    public static final String[] playerLabels = {
            "Controller 1",
            "Controller 2",
            "Controller 3",
            "Controller 4",
    };

    @Override
    public void onCreate(Bundle icicle) {
        super.onCreate(icicle);

        getWindow().setFlags(WindowManager.LayoutParams.FLAG_BLUR_BEHIND,
                WindowManager.LayoutParams.FLAG_BLUR_BEHIND);

        drawListAdapter();
    }

	private void drawListAdapter() {
		if (keyLabelsAdapter == null) {
			// Usamos el layout estándar de Android sin sobreescribir el getView
			keyLabelsAdapter = new ArrayAdapter<String>(
				this,
				android.R.layout.simple_list_item_1,
				DefineKeys.playerLabels
			);
			setListAdapter(keyLabelsAdapter);
		} else {
			keyLabelsAdapter.notifyDataSetChanged();
		}
	}

	@Override
    public void onListItemClick(ListView parent, View v, int position, long id) {
		controllerIndex = position;
        startActivityForResult(new Intent(this, ListKeys.class).putExtra(
                    "controllerIndex", controllerIndex), 0);
    }

    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);

        drawListAdapter();
    }
}

