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

// Allow use of stuff in <time.h>
#define FORBIDDEN_SYMBOL_EXCEPTION_time_h

// Disable printf override in common/forbidden.h to avoid
// clashes with log.h from the Android SDK.
// That header file uses
//   __attribute__ ((format(printf, 3, 4)))
// which gets messed up by our override mechanism; this could
// be avoided by either changing the Android SDK to use the equally
// legal and valid
//   __attribute__ ((format(printf, 3, 4)))
// or by refining our printf override to use a varadic macro
// (which then wouldn't be portable, though).
// Anyway, for now we just disable the printf override globally
// for the Android port
#define FORBIDDEN_SYMBOL_EXCEPTION_printf

#include "backends/platform/android/android.h"
#include "backends/platform/android/jni-android.h"
#include "backends/graphics/android/android-graphics.h"
#include "backends/graphics/opengl/pipelines/pipeline.h"
#include "backends/graphics/opengl/texture.h"

#include "graphics/blit.h"
#include "graphics/managed_surface.h"

#include <android/native_window.h>

//
// AndroidGraphicsManager
//
AndroidGraphicsManager::AndroidGraphicsManager() :
	_touchcontrols(nullptr),
	_old_touch_mode(OSystem_Android::TOUCH_MODE_TOUCHPAD),
	_bottomScreenChangeId(-1),
	_bottomWidth(0),
	_bottomHeight(0),
	_bottomNeedsRedraw(false) {
	ENTER();

	// Initialize our OpenGL ES context.
	initSurface();

	_rendering3d = (_renderer3d != nullptr);
	// maybe in 3D, not in GUI
	dynamic_cast<OSystem_Android *>(g_system)->applyTouchSettings(_rendering3d, false);
	dynamic_cast<OSystem_Android *>(g_system)->applyOrientationSettings();
}

AndroidGraphicsManager::~AndroidGraphicsManager() {
	ENTER();

	deinitSurface();

	delete _touchcontrols;
}

void AndroidGraphicsManager::initSurface() {
	LOGD("initializing 2D surface");

	assert(!JNI::haveSurface());
	if (!JNI::initSurface()) {
		error("JNI::initSurface failed");
	}

	if (JNI::egl_bits_per_pixel == 16) {
		// We default to RGB565 and RGBA5551 which is closest to what we setup in Java side
		notifyContextCreate(OpenGL::kContextGLES2,
				new OpenGL::Backbuffer(),
				Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0),
				Graphics::PixelFormat(2, 5, 5, 5, 1, 11, 6, 1, 0));
	} else {
		// If not 16, this must be 24 or 32 bpp so make use of them
		notifyContextCreate(OpenGL::kContextGLES2,
				new OpenGL::Backbuffer(),
				OpenGL::Texture::getRGBPixelFormat(),
				OpenGL::Texture::getRGBAPixelFormat()
		);
	}

	if (_touchcontrols) {
		_touchcontrols->recreate();
		_touchcontrols->updateGLTexture();
	} else {
		_touchcontrols = createSurface(_defaultFormatAlpha);
	}
	dynamic_cast<OSystem_Android *>(g_system)->getTouchControls().setDrawer(
	    this, JNI::egl_surface_width, JNI::egl_surface_height);

	handleResize(JNI::egl_surface_width, JNI::egl_surface_height);
}

void AndroidGraphicsManager::deinitSurface() {
	if (!JNI::haveSurface())
		return;

	LOGD("deinitializing 2D surface");

	// Deregister us from touch control
	dynamic_cast<OSystem_Android *>(g_system)->getTouchControls().setDrawer(
	    nullptr, 0, 0);
	if (_touchcontrols) {
		_touchcontrols->destroy();
	}

	notifyContextDestroy();

	JNI::deinitSurface();
}

void AndroidGraphicsManager::resizeSurface() {

	// If we had lost surface just init it again
	if (!JNI::haveSurface()) {
		initSurface();
		return;
	}

	// Recreate the EGL surface, context is preserved
	JNI::deinitSurface();
	if (!JNI::initSurface()) {
		error("JNI::initSurface failed");
	}

	dynamic_cast<OSystem_Android *>(g_system)->getTouchControls().setDrawer(
	    this, JNI::egl_surface_width, JNI::egl_surface_height);

	handleResize(JNI::egl_surface_width, JNI::egl_surface_height);
}


void AndroidGraphicsManager::updateScreen() {
	//ENTER();

	if (!JNI::haveSurface())
		return;

	// Sets _forceRedraw if needed
	dynamic_cast<OSystem_Android *>(g_system)->getTouchControls().beforeDraw();

	syncBottomScreen();

	// The base class clears these, so check them first
	const bool bottomDirty = _bottomNeedsRedraw || _forceRedraw || _cursorNeedsRedraw ||
		(_gameScreen && _gameScreen->isDirty());

	OpenGLGraphicsManager::updateScreen();

	if (bottomDirty && isSecondScreenActive()) {
		drawBottomScreen();
		_bottomNeedsRedraw = false;
	} else if (_bottomNeedsRedraw && _bottomWidth > 0 && _gameScreen) {
		// The game stopped using the second screen: blank it rather than
		// leave the last interface there
		drawBottomScreen();
		_bottomNeedsRedraw = false;
	}
}

bool AndroidGraphicsManager::isSecondScreenActive() const {
	return !_secondScreenPanels.empty() && _bottomWidth > 0 && _bottomHeight > 0 &&
		_gameScreen && _rotationMode == Common::kRotationNormal;
}

void AndroidGraphicsManager::setSecondScreenLayout(const Common::Array<Common::Rect> &panels, const Common::Array<bool> &besidePrevious) {
	Common::Array<bool> beside;
	for (uint i = 0; i < panels.size(); i++)
		beside.push_back(i > 0 && i < besidePrevious.size() && besidePrevious[i]);
	if (panels == _secondScreenPanels && beside == _secondScreenBeside)
		return;

	_secondScreenPanels = panels;
	_secondScreenBeside = beside;
	for (uint i = 0; i < panels.size(); i++)
		LOGD("second screen panel %u: (%d,%d)-(%d,%d)%s", i, panels[i].left, panels[i].top, panels[i].right, panels[i].bottom,
		     beside[i] ? " beside the previous one" : "");

	recalculateDisplayAreas();
	recalculateCursorScaling();
	_bottomNeedsRedraw = true;
}

// Pick up a new, resized or lost second screen surface from the Java side
void AndroidGraphicsManager::syncBottomScreen() {
	if (_bottomScreenChangeId == JNI::bottom_screen_changeid)
		return;

	const bool hadBottomScreen = (_bottomWidth > 0);

	ANativeWindow *window = JNI::lockBottomScreen();
	_bottomScreenChangeId = JNI::bottom_screen_changeid;
	_bottomWidth = _bottomHeight = 0;
	if (window) {
		_bottomWidth = ANativeWindow_getWidth(window);
		_bottomHeight = ANativeWindow_getHeight(window);
		if (ANativeWindow_setBuffersGeometry(window, _bottomWidth, _bottomHeight, WINDOW_FORMAT_RGBX_8888) != 0)
			_bottomWidth = _bottomHeight = 0;
	}
	JNI::unlockBottomScreen();

	LOGD("second screen is now %dx%d", _bottomWidth, _bottomHeight);

	recalculateDisplayAreas();
	recalculateCursorScaling();
	_bottomNeedsRedraw = true;

	// The default touch mode depends on whether there is a second screen
	if (hadBottomScreen != (_bottomWidth > 0))
		applyTouchSettings();
}

// Stack the panels top to bottom on the second screen, as large as they fit.
// Panels marked as beside the previous one share its row. Full width rows
// (e.g. the sentence line) are scaled to the screen width, narrower ones
// (e.g. verbs and inventory split apart) share one larger scale.
void AndroidGraphicsManager::layoutBottomScreen() {
	_bottomPanelRects.clear();
	if (_secondScreenPanels.empty() || _bottomWidth <= 0 || _bottomHeight <= 0 || !_gameScreen)
		return;

	const int gameWidth = getWidth();
	const int gameHeight = getHeight();

	// Height of a game pixel relative to its width, from aspect ratio correction
	const float pixelAspect = (float)intToFrac(gameWidth) / gameHeight / getDesiredGameAspectRatio();

	const float margin = 0.96f;
	const int columnGap = _bottomWidth / 40;

	struct Row {
		uint first, last;
		int width, height; // in game pixels
		float scale;
	};
	Common::Array<Row> rows;
	for (uint i = 0; i < _secondScreenPanels.size(); i++) {
		const Common::Rect &panel = _secondScreenPanels[i];
		if (!rows.empty() && _secondScreenBeside[i]) {
			Row &row = rows.back();
			row.last = i;
			row.width += panel.width();
			row.height = MAX<int>(row.height, panel.height());
		} else {
			Row row = { i, i, panel.width(), panel.height(), 0.0f };
			rows.push_back(row);
		}
	}

	const float fullScale = margin * _bottomWidth / gameWidth;
	float splitScale = 0.0f;
	for (uint i = 0; i < rows.size(); i++) {
		if (rows[i].width > 0 && rows[i].width < gameWidth) {
			const float s = (margin * _bottomWidth - columnGap * (rows[i].last - rows[i].first)) / rows[i].width;
			splitScale = (splitScale == 0.0f) ? s : MIN(splitScale, s);
		}
	}

	float totalHeight = 0.0f;
	for (uint i = 0; i < rows.size(); i++) {
		rows[i].scale = (rows[i].width < gameWidth) ? splitScale : fullScale;
		totalHeight += rows[i].height * rows[i].scale * pixelAspect;
	}

	// Rows showing the same game rows are one line wrapped in two, so keep
	// them close
	const int gap = _bottomHeight / 40;
	Common::Array<int> gaps;
	int totalGaps = 0;
	for (uint i = 1; i < rows.size(); i++) {
		const Common::Rect &prev = _secondScreenPanels[rows[i - 1].last];
		const Common::Rect &panel = _secondScreenPanels[rows[i].first];
		const bool continued = (panel.top == prev.top && panel.bottom == prev.bottom);
		gaps.push_back(continued ? gap / 4 : gap);
		totalGaps += gaps.back();
	}
	const float available = margin * _bottomHeight - totalGaps;
	const float shrink = (totalHeight > available) ? available / totalHeight : 1.0f;

	int usedHeight = totalGaps;
	int splitWidth = 0;
	Common::Array<Common::Point> sizes;
	for (uint i = 0; i < rows.size(); i++) {
		int rowWidth = columnGap * (rows[i].last - rows[i].first);
		for (uint j = rows[i].first; j <= rows[i].last; j++) {
			const Common::Rect &panel = _secondScreenPanels[j];
			const int w = MAX(1, (int)(panel.width() * rows[i].scale * shrink));
			const int h = MAX(1, (int)(panel.height() * rows[i].scale * shrink * pixelAspect));
			sizes.push_back(Common::Point(w, h));
			rowWidth += w;
		}
		usedHeight += (int)(rows[i].height * rows[i].scale * shrink * pixelAspect);
		if (rows[i].width < gameWidth)
			splitWidth = MAX(splitWidth, rowWidth);
	}

	// Narrower rows line up on their left edges, in a centered column
	const int splitLeft = (_bottomWidth - splitWidth) / 2;
	int y = (_bottomHeight - usedHeight) / 2;
	for (uint i = 0; i < rows.size(); i++) {
		int x = (rows[i].width < gameWidth) ? splitLeft : (_bottomWidth - sizes[rows[i].first].x) / 2;
		for (uint j = rows[i].first; j <= rows[i].last; j++) {
			_bottomPanelRects.push_back(Common::Rect(x, y, x + sizes[j].x, y + sizes[j].y));
			x += sizes[j].x + columnGap;
		}
		if (i < gaps.size())
			y += (int)(rows[i].height * rows[i].scale * shrink * pixelAspect) + gaps[i];
	}
}

static inline uint32 packRGBX(byte r, byte g, byte b) {
	// WINDOW_FORMAT_RGBX_8888 is R, G, B, X in memory
#ifdef SCUMM_LITTLE_ENDIAN
	return r | (g << 8) | (b << 16) | 0xFF000000;
#else
	return (r << 24) | (g << 16) | (b << 8) | 0xFF;
#endif
}

void AndroidGraphicsManager::drawBottomScreen() {
	if (_bottomPanelRects.size() != _secondScreenPanels.size())
		return;

	const Graphics::Surface *src = _gameScreen->getSurface();
	if (!src || !src->getPixels())
		return;

	ANativeWindow *window = JNI::lockBottomScreen();
	ANativeWindow_Buffer buffer;
	if (!window || ANativeWindow_lock(window, &buffer, nullptr) != 0) {
		JNI::unlockBottomScreen();
		return;
	}

	const int width = MIN<int>(buffer.width, _bottomWidth);
	const int height = MIN<int>(buffer.height, _bottomHeight);
	uint32 *dst = (uint32 *)buffer.bits;
	const uint32 black = packRGBX(0, 0, 0);
	for (int y = 0; y < height; y++) {
		uint32 *row = dst + y * buffer.stride;
		for (int x = 0; x < width; x++)
			row[x] = black;
	}

	uint32 palette[256];
	if (src->format.isCLUT8()) {
		for (int i = 0; i < 256; i++)
			palette[i] = packRGBX(_gamePalette[i * 3], _gamePalette[i * 3 + 1], _gamePalette[i * 3 + 2]);
	}

	// Maps a game screen pixel to a packed color
	auto gamePixel = [&](int x, int y) -> uint32 {
		const byte *p = (const byte *)src->getBasePtr(x, y);
		if (src->format.isCLUT8())
			return palette[*p];
		byte r, g, b;
		src->format.colorToRGB(src->format.bytesPerPixel == 2 ? *(const uint16 *)p : *(const uint32 *)p, r, g, b);
		return packRGBX(r, g, b);
	};

	// The cursor, in game coordinates, when it is over the second screen
	const Graphics::Surface *cursor = nullptr;
	Common::Point cursorHotspot;
	Common::Rect cursorRect;
	uint32 cursorPalette[256];
	if (_cursorVisible && _cursor && !_overlayVisible) {
		cursor = _cursor->getSurface();
		const Common::Point pos = convertWindowToVirtual(_cursorX, _cursorY);
		cursorHotspot = pos;
		cursorRect = Common::Rect(pos.x - _cursorHotspotX, pos.y - _cursorHotspotY,
		                          pos.x - _cursorHotspotX + cursor->w, pos.y - _cursorHotspotY + cursor->h);
		if (cursor->format.isCLUT8()) {
			const byte *pal = _cursorPaletteEnabled ? _cursorPalette : _gamePalette;
			for (int i = 0; i < 256; i++)
				cursorPalette[i] = packRGBX(pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]);
		}
	}

	// Returns whether the cursor covers this cursor pixel, and its color
	auto cursorPixel = [&](int x, int y, uint32 &color) -> bool {
		const byte *p = (const byte *)cursor->getBasePtr(x, y);
		if (cursor->format.isCLUT8()) {
			if (_cursorUseKey && *p == _cursorKeyColor)
				return false;
			color = cursorPalette[*p];
			return true;
		}
		const uint32 raw = cursor->format.bytesPerPixel == 2 ? *(const uint16 *)p : *(const uint32 *)p;
		if (_cursorUseKey && raw == _cursorKeyColor)
			return false;
		byte a, r, g, b;
		cursor->format.colorToARGB(raw, a, r, g, b);
		if (a < 128)
			return false;
		color = packRGBX(r, g, b);
		return true;
	};

	for (uint i = 0; i < _secondScreenPanels.size(); i++) {
		const Common::Rect &from = _secondScreenPanels[i];
		Common::Rect to = _bottomPanelRects[i];
		to.clip(Common::Rect(width, height));
		if (to.isEmpty() || from.isEmpty())
			continue;

		const Common::Rect &full = _bottomPanelRects[i];
		for (int y = to.top; y < to.bottom; y++) {
			const int sy = from.top + (y - full.top) * from.height() / full.height();
			if (sy < 0 || sy >= src->h)
				continue;
			uint32 *row = dst + y * buffer.stride;
			for (int x = to.left; x < to.right; x++) {
				const int sx = from.left + (x - full.left) * from.width() / full.width();
				if (sx < 0 || sx >= src->w)
					continue;
				row[x] = gamePixel(sx, sy);
			}
		}
	}

	// Draw the cursor over everything, scaled like the panel nearest to its
	// hotspot, so it is not cut off by the panel edges
	const int topRows = secondScreenTopRows();
	if (cursor && cursorHotspot.y >= topRows) {
		int nearest = -1;
		int nearestDistance = 0;
		for (uint i = 0; i < _secondScreenPanels.size(); i++) {
			const Common::Rect &panel = _secondScreenPanels[i];
			if (panel.isEmpty())
				continue;
			const int dx = MAX(0, MAX(panel.left - cursorHotspot.x, cursorHotspot.x - (panel.right - 1)));
			const int dy = MAX(0, MAX(panel.top - cursorHotspot.y, cursorHotspot.y - (panel.bottom - 1)));
			if (nearest < 0 || dx + dy < nearestDistance) {
				nearest = i;
				nearestDistance = dx + dy;
			}
		}

		if (nearest >= 0) {
			const Common::Rect &from = _secondScreenPanels[nearest];
			const Common::Rect &full = _bottomPanelRects[nearest];
			const float scaleX = (float)full.width() / from.width();
			const float scaleY = (float)full.height() / from.height();
			const int left = full.left + (int)((cursorRect.left - from.left) * scaleX);
			const int top = full.top + (int)((cursorRect.top - from.top) * scaleY);
			Common::Rect to(left, top, left + (int)(cursor->w * scaleX), top + (int)(cursor->h * scaleY));
			const Common::Rect scaled = to;
			to.clip(Common::Rect(width, height));
			for (int y = to.top; y < to.bottom; y++) {
				const int cy = (y - scaled.top) * cursor->h / scaled.height();
				uint32 *row = dst + y * buffer.stride;
				for (int x = to.left; x < to.right; x++) {
					uint32 color;
					if (cursorPixel((x - scaled.left) * cursor->w / scaled.width(), cy, color))
						row[x] = color;
				}
			}
		}
	}

	ANativeWindow_unlockAndPost(window);
	JNI::unlockBottomScreen();
}

bool AndroidGraphicsManager::bottomScreenToWindow(int x, int y, Common::Point &window) const {
	if (!isSecondScreenActive() || _overlayInGUI || _bottomPanelRects.size() != _secondScreenPanels.size())
		return false;

	// Use the panel that was touched, or the nearest one for touches in the gaps
	uint best = 0;
	int bestDistance = 1 << 30;
	for (uint i = 0; i < _bottomPanelRects.size(); i++) {
		const Common::Rect &r = _bottomPanelRects[i];
		const int dx = (x < r.left) ? r.left - x : (x >= r.right ? x - r.right + 1 : 0);
		const int dy = (y < r.top) ? r.top - y : (y >= r.bottom ? y - r.bottom + 1 : 0);
		if (dx + dy < bestDistance) {
			best = i;
			bestDistance = dx + dy;
		}
	}

	const Common::Rect &to = _bottomPanelRects[best];
	const Common::Rect &from = _secondScreenPanels[best];
	x = CLIP<int>(x, to.left, to.right - 1);
	y = CLIP<int>(y, to.top, to.bottom - 1);
	const int gameX = from.left + (x - to.left) * from.width() / to.width();
	const int gameY = from.top + (y - to.top) * from.height() / to.height();

	window = convertVirtualToWindow(gameX, gameY);
	return true;
}

// Number of game screen rows left on the main screen, or 0 when the second
// screen is not in use
int AndroidGraphicsManager::secondScreenTopRows() const {
	if (!isSecondScreenActive())
		return 0;

	const int gameHeight = getHeight();
	int topHeight = gameHeight;
	for (uint i = 0; i < _secondScreenPanels.size(); i++)
		topHeight = MIN<int>(topHeight, _secondScreenPanels[i].top);
	if (topHeight <= 0 || topHeight >= gameHeight)
		return 0;
	return topHeight;
}

// Scale the game so the part above the second screen panels fills the main
// screen. The rest of the game screen ends up below the window's bottom edge.
void AndroidGraphicsManager::adjustGameDrawRect(Common::Rect &drawRect) const {
	const int topHeight = secondScreenTopRows();
	if (!topHeight)
		return;

	const int gameHeight = getHeight();

	// Aspect ratio of what stays on the main screen
	const frac_t topAspect = getDesiredGameAspectRatio() * gameHeight / topHeight;

	int width = _windowWidth;
	int height = intToFrac(width) / topAspect;
	if (height > _windowHeight) {
		height = _windowHeight;
		width = fracToInt(height * topAspect);
	}

	drawRect.left = (_windowWidth - width) / 2;
	drawRect.right = drawRect.left + width;
	drawRect.top = (_windowHeight - height) / 2;
	drawRect.bottom = drawRect.top + height * gameHeight / topHeight;
}

void AndroidGraphicsManager::displayMessageOnOSD(const Common::U32String &msg) {
	ENTER("%s", msg.encode().c_str());

	JNI::displayMessageOnOSD(msg);
}

void AndroidGraphicsManager::recalculateDisplayAreas() {
	Common::Rect oldDrawRect = _activeArea.drawRect;

	OpenGLGraphicsManager::recalculateDisplayAreas();

	// Aspect ratio correction changes how tall the second screen panels are
	layoutBottomScreen();
	_bottomNeedsRedraw = true;

	// The game draw rect reaches past the window's bottom edge, but when the
	// window is taller than the room, the top rows of the interface would
	// still show below it. Clip the game to the room.
	const int topRows = secondScreenTopRows();
	if (topRows > 0) {
		const int visibleBottom = _gameDrawRect.top + _gameDrawRect.height() * topRows / getHeight();
		_targetBuffer->setScissorBox(_gameDrawRect.left,
		                             _windowHeight - visibleBottom,
		                             _gameDrawRect.width(),
		                             visibleBottom - _gameDrawRect.top);
	}

	int offsetX = _activeArea.drawRect.left - oldDrawRect.left;
	int offsetY = _activeArea.drawRect.top - oldDrawRect.top;

	int newX = _cursorX + offsetX;
	int newY = _cursorY + offsetY;

	newX = CLIP<int16>(newX, _activeArea.drawRect.left, _activeArea.drawRect.right);
	newY = CLIP<int16>(newY, _activeArea.drawRect.top, _activeArea.drawRect.bottom);

	setMousePosition(newX, newY);
}

void AndroidGraphicsManager::showOverlay(bool inGUI) {
	if (_overlayVisible && inGUI == _overlayInGUI)
		return;

	// Don't change touch mode when not changing mouse coordinates
	if (inGUI) {
		_old_touch_mode = JNI::getTouchMode();
		// maybe in 3D, in overlay
		dynamic_cast<OSystem_Android *>(g_system)->applyTouchSettings(_renderer3d != nullptr, true);
		dynamic_cast<OSystem_Android *>(g_system)->applyOrientationSettings();
	} else if (_overlayInGUI) {
		// Restore touch mode active before overlay was shown
		JNI::setTouchMode(_old_touch_mode);
	}

	OpenGL::OpenGLGraphicsManager::showOverlay(inGUI);
}

void AndroidGraphicsManager::hideOverlay() {
	if (!_overlayVisible)
		return;

	if (_overlayInGUI) {
		// Restore touch mode active before overlay was shown
		JNI::setTouchMode(_old_touch_mode);
		dynamic_cast<OSystem_Android *>(g_system)->applyOrientationSettings();
	}

	OpenGL::OpenGLGraphicsManager::hideOverlay();
}

float AndroidGraphicsManager::getHiDPIScreenFactor() const {
	JNI::DPIValues dpi;
	JNI::getDPI(dpi);
	// Scale down the Android factor else the GUI is too big and
	// there is not much options to go smaller
	return dpi[2] / 1.2f;
}

bool AndroidGraphicsManager::loadVideoMode(uint requestedWidth, uint requestedHeight, bool resizable, int antialiasing) {
	ENTER("%d, %d, %d, %d", requestedWidth, requestedHeight, resizable, antialiasing);

	// As GLES2 provides FBO, OpenGL graphics manager must ask us for a resizable surface
	assert(resizable);
	if (antialiasing != 0) {
		warning("Requesting antialiased video mode while not available");
	}

	const bool render3d = (_renderer3d != nullptr);
	if (_rendering3d != render3d) {
		_rendering3d = render3d;
		// 3D status changed: refresh the touch mode
		applyTouchSettings();
	}

	// We get this whenever a new resolution is requested. Since Android is
	// using a fixed output size we do nothing like that here.
	return true;
}

void AndroidGraphicsManager::refreshScreen() {
	//ENTER();

	// Last minute draw of touch controls
	dynamic_cast<OSystem_Android *>(g_system)->getTouchControls().draw();

	JNI::swapBuffers();
}

void AndroidGraphicsManager::applyTouchSettings() const {
	// maybe in 3D, maybe in GUI
	dynamic_cast<OSystem_Android *>(g_system)->applyTouchSettings(_renderer3d != nullptr, _overlayVisible && _overlayInGUI);
}

void AndroidGraphicsManager::syncVirtkeyboardState(bool virtkeybd_on) {
	_screenAlign = SCREEN_ALIGN_CENTER;
	if (virtkeybd_on) {
		_screenAlign |= SCREEN_ALIGN_TOP;
	} else {
		_screenAlign |= SCREEN_ALIGN_MIDDLE;
	}
	recalculateDisplayAreas();
	_forceRedraw = true;
}

void AndroidGraphicsManager::touchControlInitSurface(const Graphics::ManagedSurface &surf) {
	if (_touchcontrols->getWidth() == (uint)surf.w && _touchcontrols->getHeight() == (uint)surf.h) {
		return;
	}

	_touchcontrols->allocate(surf.w, surf.h);
	Graphics::Surface *dst = _touchcontrols->getSurface();

	Graphics::crossBlit(
			(byte *)dst->getPixels(), (const byte *)surf.getPixels(),
			dst->pitch, surf.pitch,
			surf.w, surf.h,
			dst->format, surf.format);
	_touchcontrols->updateGLTexture();
}

void AndroidGraphicsManager::touchControlDraw(uint8 alpha, int16 x, int16 y, int16 w, int16 h, const Common::Rect &clip) {
	_targetBuffer->enableBlend(OpenGL::Framebuffer::kBlendModeTraditionalTransparency);
	OpenGL::Pipeline *pipeline = getPipeline();
	pipeline->activate();
	if (alpha != 255) {
		pipeline->setColor(1.0f, 1.0f, 1.0f, alpha / 255.0f);
	}
	pipeline->drawTexture(_touchcontrols->getGLTexture(),
	                      x, y, w, h, clip);
	if (alpha != 255) {
		pipeline->setColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
}

void AndroidGraphicsManager::touchControlNotifyChanged() {
	// Make sure we redraw the screen
	_forceRedraw = true;
}

bool AndroidGraphicsManager::notifyMousePosition(Common::Point &mouse) {
	mouse.x = CLIP<int16>(mouse.x, _activeArea.drawRect.left, _activeArea.drawRect.right);
	mouse.y = CLIP<int16>(mouse.y, _activeArea.drawRect.top, _activeArea.drawRect.bottom);

	setMousePosition(mouse.x, mouse.y);
	mouse = convertWindowToVirtual(mouse.x, mouse.y);

	return true;
}

WindowedGraphicsManager::Insets AndroidGraphicsManager::getSafeAreaInsets() const {
	return WindowedGraphicsManager::Insets{
		(int16)JNI::cutout_insets[0], (int16)JNI::cutout_insets[1],
		(int16)JNI::cutout_insets[2], (int16)JNI::cutout_insets[3]};
}
