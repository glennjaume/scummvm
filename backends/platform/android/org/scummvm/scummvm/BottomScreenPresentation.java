/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

package org.scummvm.scummvm;

import android.annotation.TargetApi;
import android.app.Presentation;
import android.content.Context;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.Display;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;

/**
 * Shows the game's own interface (sentence line, verbs, inventory) on a
 * second physical screen, such as the bottom screen of dual-screen handhelds.
 *
 * The native side draws into this window's surface and maps touches on it
 * back to the game. The window never takes input focus, so a controller
 * keeps driving the game while the second screen is touched.
 */
@TargetApi(Build.VERSION_CODES.JELLY_BEAN_MR1)
public class BottomScreenPresentation extends Presentation implements SurfaceHolder.Callback, View.OnTouchListener {
	private final ScummVM _scummvm;

	public BottomScreenPresentation(Context outerContext, Display display, ScummVM scummvm) {
		super(outerContext, display);
		_scummvm = scummvm;
	}

	/**
	 * Find a second screen to use, or return null when there is none.
	 */
	public static Display findSecondScreen(Context context) {
		if (Build.VERSION.SDK_INT < Build.VERSION_CODES.JELLY_BEAN_MR1) {
			return null;
		}

		DisplayManager displayManager = (DisplayManager) context.getSystemService(Context.DISPLAY_SERVICE);
		if (displayManager == null) {
			return null;
		}

		Display[] displays = displayManager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION);
		if (displays.length > 0) {
			return displays[0];
		}

		// Some devices don't flag their built-in second screen for presentations
		for (Display display : displayManager.getDisplays()) {
			if (display.getDisplayId() != Display.DEFAULT_DISPLAY) {
				return display;
			}
		}
		return null;
	}

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);

		getWindow().addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
		                     | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

		SurfaceView view = new SurfaceView(getContext());
		view.getHolder().addCallback(this);
		view.setOnTouchListener(this);
		setContentView(view);
	}

	@Override
	public void surfaceCreated(SurfaceHolder holder) {
	}

	@Override
	public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
		Log.d(ScummVM.LOG_TAG, "second screen surface " + width + "x" + height);
		_scummvm.setBottomScreen(holder.getSurface());
	}

	@Override
	public void surfaceDestroyed(SurfaceHolder holder) {
		_scummvm.setBottomScreen(null);
	}

	@Override
	public boolean onTouch(View v, MotionEvent e) {
		// Only the first finger acts as the mouse
		if (e.getPointerCount() > 1 && e.getActionMasked() != MotionEvent.ACTION_CANCEL) {
			return true;
		}

		switch (e.getActionMasked()) {
		case MotionEvent.ACTION_DOWN:
		case MotionEvent.ACTION_MOVE:
		case MotionEvent.ACTION_UP:
		case MotionEvent.ACTION_CANCEL:
			_scummvm.bottomScreenTouch(e.getActionMasked(), (int) e.getX(), (int) e.getY());
			break;
		default:
			break;
		}
		return true;
	}
}
