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

package com.seleuco.mame4droid.input;

public interface IController {


    final public static int UP_VALUE = 0x1;
    final public static int LEFT_VALUE = 0x4;
    final public static int DOWN_VALUE = 0x10;
    final public static int RIGHT_VALUE = 0x40;
    final public static int START_VALUE = 1 << 8;
    final public static int COIN_VALUE = 1 << 9;
	final public static int A_VALUE = 1 << 10;
	final public static int B_VALUE = 1 << 11;
	final public static int C_VALUE = 1 << 12;
	final public static int D_VALUE = 1 << 13;
	final public static int E_VALUE = 1 << 14;
	final public static int F_VALUE = 1 << 15;
	final public static int G_VALUE = 1 << 16;
	final public static int H_VALUE = 1 << 17;
    final public static int EXIT_VALUE = 1 << 20;
    final public static int OPTION_VALUE = 1 << 21;

    final public static int STICK_NONE = 0;
    final public static int STICK_UP_LEFT = 1;
    final public static int STICK_UP = 2;
    final public static int STICK_UP_RIGHT = 3;
    final public static int STICK_LEFT = 4;
    final public static int STICK_RIGHT = 5;
    final public static int STICK_DOWN_LEFT = 6;
    final public static int STICK_DOWN = 7;
    final public static int STICK_DOWN_RIGHT = 8;

    final public static int NUM_BUTTONS = 12;

    final public static int BTN_A = 2;
    final public static int BTN_B = 3;
    final public static int BTN_C = 1;
    final public static int BTN_D = 0;
    final public static int BTN_E = 4;
    final public static int BTN_F = 5;
    final public static int BTN_EXIT = 6;
    final public static int BTN_OPTION = 7;
    final public static int BTN_COIN = 8;
    final public static int BTN_START = 9;
	final public static int BTN_G = 10;
	final public static int BTN_H = 11;

    final public static int BTN_PRESS_STATE = 0;
    final public static int BTN_NO_PRESS_STATE = 1;

    //http://stackoverflow.com/questions/2949036/android-how-to-get-a-custom-view-to-redraw-partially
    //http://stackoverflow.com/questions/3874051/getting-the-dirty-region-inside-draw

}
