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

// Support for devices with a second screen, such as dual-screen handhelds:
// the verbs, inventory and dialog choices go on the second screen, and the
// d-pad can step between them.

#include "common/events.h"
#include "common/system.h"
#include "graphics/surface.h"

#include "agos/agos.h"
#include "agos/intern.h"

namespace AGOS {

// Rows below the room, where Simon the Sorcerer 1 and 2 keep their interface
static const int kInterfaceTop = 134;

bool AGOSEngine::usesSecondScreen() const {
	return (getGameType() == GType_SIMON1 || getGameType() == GType_SIMON2) &&
	       _screenWidth == 320 && _screenHeight == 200 &&
	       (int)_system->getWidth() == _screenWidth && (int)_system->getHeight() == _screenHeight;
}

bool AGOSEngine::getInterfaceBoxes(Common::Array<Common::Rect> &boxes, bool &dialog, Common::Rect &inventory,
                                   Common::Array<Common::Rect> *verbBoxes) const {
	// Simon 2 sometimes uses the whole screen for the room
	if (getGameType() == GType_SIMON2 && const_cast<AGOSEngine *>(this)->getBitFlag(79))
		return false;

	// Nothing can be picked while the mouse is off, e.g. in the intro and
	// cutscenes
	if (_mouseHideCount)
		return true;

	const Common::Rect area(0, kInterfaceTop, _screenWidth, _screenHeight);

	Common::Array<Common::Rect> verbs, items, texts, others;
	for (uint i = 0; i < ARRAYSIZE(_hitAreas); i++) {
		const HitArea &ha = _hitAreas[i];
		if (!(ha.flags & kBFBoxInUse) || (ha.flags & kBFBoxDead))
			continue;
		// The save and load dialog covers the whole screen. Its file slots
		// are only enabled while it is open.
		if (ha.id >= 208 && ha.id <= 213)
			return false;
		if (ha.id >= 200 && ha.id <= 213)
			continue;
		// Skip boxes in the room that only touch the interface
		if (ha.y < kInterfaceTop - 4)
			continue;
		const Common::Rect r = Common::Rect(ha.x, ha.y, ha.x + ha.width, ha.y + ha.height).findIntersectingRect(area);
		if (r.isEmpty())
			continue;
		// Boxes spanning most of the interface, such as the one behind the
		// whole inventory, are neither verbs nor items
		const bool wide = r.width() >= _screenWidth * 3 / 4;
		if (ha.flags & kBFTextBox)
			texts.push_back(r);
		else if (ha.id >= 101 && ha.id <= 112)
			verbs.push_back(r);
		else if ((ha.flags & kBFBoxItem) || ha.id == 0x7FFB || ha.id == 0x7FFC || ha.id == 0x7FFD) {
			if (!wide)
				items.push_back(r);
		} else
			others.push_back(r);
	}

	dialog = false;
	inventory = Common::Rect();
	if (verbBoxes)
		*verbBoxes = verbs;
	if (!texts.empty()) {
		// During conversations, only the choices can be picked
		dialog = true;
		boxes = texts;
	} else if (!verbs.empty()) {
		boxes = verbs;
		for (uint i = 0; i < items.size(); i++) {
			boxes.push_back(items[i]);
			if (inventory.isEmpty())
				inventory = items[i];
			else
				inventory.extend(items[i]);
		}
		for (uint i = 0; i < others.size(); i++) {
			if (others[i].width() < _screenWidth * 3 / 4)
				boxes.push_back(others[i]);
		}
	} else if (!others.empty()) {
		// Without verbs, the interface shows choices, such as yes and no
		dialog = true;
		boxes = others;
	}

	// Choices: leave out boxes around other boxes, which only catch stray
	// clicks. Verbs: leave out boxes hidden inside others, e.g. Simon 2 has
	// a walk box under the look icon.
	for (uint i = 0; i < boxes.size();) {
		bool drop = false;
		for (uint j = 0; j < boxes.size() && !drop; j++) {
			if (j == i)
				continue;
			if (boxes[i] == boxes[j])
				drop = (j < i);
			else if (dialog)
				drop = boxes[i].contains(boxes[j]);
			else
				drop = boxes[j].contains(boxes[i]);
		}
		if (drop)
			boxes.remove_at(i);
		else
			i++;
	}
	return true;
}

void AGOSEngine::logInterfaceBoxes() {
	// Log the interface boxes when they change, to help tune the layout
	Common::String dump;
	for (uint i = 0; i < ARRAYSIZE(_hitAreas); i++) {
		const HitArea &ha = _hitAreas[i];
		if (!(ha.flags & kBFBoxInUse) || ha.y + ha.height <= kInterfaceTop)
			continue;
		dump += Common::String::format(" %d:(%d,%d,%dx%d)%x", ha.id, ha.x, ha.y, ha.width, ha.height, ha.flags);
	}
	dump += Common::String::format(" mouse %s", _mouseHideCount ? "off" : "on");
	if (dump != _secondScreenBoxes) {
		_secondScreenBoxes = dump;
		debug("AGOS second screen boxes:%s", dump.c_str());
	}
}

void AGOSEngine::updateSecondScreenLayout() {
	if (!usesSecondScreen()) {
		if (!_secondScreenPanels.empty()) {
			_secondScreenPanels.clear();
			_secondScreenBeside.clear();
			_system->setSecondScreenLayout(_secondScreenPanels);
		}
		return;
	}

	logInterfaceBoxes();

	const Common::Rect area(0, kInterfaceTop, _screenWidth, _screenHeight);
	Common::Array<Common::Rect> boxes, verbs;
	bool dialog = false;
	Common::Rect inventory;
	Common::Array<Common::Rect> panels;
	Common::Array<bool> beside;
	if (!getInterfaceBoxes(boxes, dialog, inventory, &verbs)) {
		// Show the whole game on the main screen
	} else if (boxes.empty()) {
		// Nothing to pick (e.g. a cutscene): keep the layout we had, so the
		// second screen does not jump around. Before the interface first
		// appears (e.g. the intro), show the whole game on the main screen.
		// Once the choices are gone, go back to the verbs.
		if (!_secondScreenDialog || _secondScreenGameplayPanels.empty())
			return;
		panels = _secondScreenGameplayPanels;
		beside = _secondScreenGameplayBeside;
	} else if (dialog) {
		// Keep each choice only as wide as its text, so the second screen
		// can show it larger. The text is looked up again each frame, as it
		// may be printed after the box is set up.
		if (boxes != _secondScreenChoices) {
			_secondScreenChoices = boxes;
			_secondScreenChoiceEnds.clear();
			_secondScreenChoiceEnds.resize(boxes.size(), 0);
		}
		for (uint i = 0; i < boxes.size(); i++) {
			_secondScreenChoiceEnds[i] = MAX(_secondScreenChoiceEnds[i], findTextEnd(boxes[i]));
			if (_secondScreenChoiceEnds[i] > boxes[i].left)
				boxes[i].right = MIN<int>(boxes[i].right, _secondScreenChoiceEnds[i] + 4);
		}

		// One panel per choice, stacked in reading order
		for (uint i = 0; i < boxes.size(); i++) {
			uint pos = 0;
			while (pos < panels.size() && (panels[pos].top < boxes[i].top ||
			       (panels[pos].top == boxes[i].top && panels[pos].left < boxes[i].left)))
				pos++;
			panels.insert_at(pos, boxes[i]);
		}
	} else {
		int boxesTop = area.bottom;
		for (uint i = 0; i < boxes.size(); i++)
			boxesTop = MIN<int>(boxesTop, boxes[i].top);

		// The sentence line sits above the verbs and spans the full width
		if (boxesTop - area.top >= 4)
			panels.push_back(Common::Rect(0, area.top, _screenWidth, boxesTop));
		else
			boxesTop = area.top;

		// Simon 2 has verbs on both sides of the inventory: put both groups
		// of verbs in one row, and the inventory below them
		int leftEnd = -1, rightStart = -1;
		for (uint i = 0; i < verbs.size(); i++) {
			const Common::Rect &r = verbs[i];
			if (r.left + r.right < _screenWidth)
				leftEnd = MAX<int>(leftEnd, r.right);
			else
				rightStart = (rightStart < 0) ? r.left : MIN<int>(rightStart, r.left);
		}

		if (leftEnd > 0 && rightStart > 0 && rightStart - leftEnd >= _screenWidth / 4) {
			int invLeft = leftEnd, invRight = rightStart;
			if (getGameType() == GType_SIMON2) {
				// The inventory frame is drawn from x 80 to 246
				invLeft = CLIP(80, leftEnd, rightStart);
				invRight = CLIP(246, invLeft, rightStart);
			}
			if (!inventory.isEmpty()) {
				invLeft = MIN<int>(invLeft, inventory.left);
				invRight = MAX<int>(invRight, inventory.right);
			}
			panels.push_back(Common::Rect(0, boxesTop, invLeft, area.bottom));
			panels.push_back(Common::Rect(invRight, boxesTop, _screenWidth, area.bottom));
			beside.resize(panels.size());
			beside.back() = true;
			panels.push_back(Common::Rect(invLeft, boxesTop, invRight, area.bottom));
		} else {
			// Split the verbs from the inventory in the empty columns
			// closest to the middle, or between two touching boxes if there
			// are none
			Common::Array<bool> used(_screenWidth, false);
			Common::Array<bool> straddled(_screenWidth, false);
			for (uint i = 0; i < boxes.size(); i++) {
				const Common::Rect &r = boxes[i];
				for (int x = r.left; x < r.right; x++) {
					used[x] = true;
					if (x > r.left)
						straddled[x] = true;
				}
			}

			int split = -1;
			for (int x = _screenWidth / 4; x < _screenWidth * 3 / 4; x++) {
				if (!used[x] && (split < 0 || ABS(x - _screenWidth / 2) < ABS(split - _screenWidth / 2)))
					split = x;
			}
			if (split >= 0) {
				int left = split, right = split;
				while (left > 0 && !used[left - 1])
					left--;
				while (right < _screenWidth && !used[right])
					right++;
				split = (left + right) / 2;
			} else {
				for (int x = _screenWidth / 4; x < _screenWidth * 3 / 4; x++) {
					if (!straddled[x] && (split < 0 || ABS(x - _screenWidth / 2) < ABS(split - _screenWidth / 2)))
						split = x;
				}
			}

			if (split > 0) {
				panels.push_back(Common::Rect(0, boxesTop, split, area.bottom));
				panels.push_back(Common::Rect(split, boxesTop, _screenWidth, area.bottom));
			} else {
				panels.push_back(Common::Rect(0, boxesTop, _screenWidth, area.bottom));
			}
		}
	}
	beside.resize(panels.size());

	_secondScreenDialog = dialog && !boxes.empty();
	if (!dialog && !boxes.empty()) {
		_secondScreenGameplayPanels = panels;
		_secondScreenGameplayBeside = beside;
	}

	if (panels == _secondScreenPanels && beside == _secondScreenBeside)
		return;
	_secondScreenPanels = panels;
	_secondScreenBeside = beside;
	_system->setSecondScreenLayout(_secondScreenPanels, _secondScreenBeside);
}

int AGOSEngine::findTextEnd(const Common::Rect &box) {
	// The right edge of what is drawn in the box: the last column with a
	// pixel other than the most common color, which is the background
	const Graphics::Surface *screen = getBackendSurface();
	if (!screen || box.right > screen->w || box.bottom > screen->h)
		return -1;

	uint counts[256] = { 0 };
	for (int y = box.top; y < box.bottom; y++) {
		const byte *row = (const byte *)screen->getBasePtr(box.left, y);
		for (int x = 0; x < box.width(); x++)
			counts[row[x]]++;
	}
	byte background = 0;
	for (int c = 1; c < 256; c++) {
		if (counts[c] > counts[background])
			background = c;
	}

	for (int x = box.right - 1; x >= box.left; x--) {
		for (int y = box.top; y < box.bottom; y++) {
			if (*(const byte *)screen->getBasePtr(x, y) != background)
				return x + 1;
		}
	}
	return -1;
}

void AGOSEngine::focusNextBox(int dirX, int dirY) {
	Common::Array<Common::Rect> boxes;
	bool dialog = false;
	Common::Rect inventory;
	if (!usesSecondScreen() || !getInterfaceBoxes(boxes, dialog, inventory) || boxes.empty())
		return;

	// Choices are shown only as wide as their text
	if (dialog && boxes == _secondScreenChoices) {
		for (uint i = 0; i < boxes.size(); i++) {
			if (_secondScreenChoiceEnds[i] > boxes[i].left)
				boxes[i].right = MIN<int>(boxes[i].right, _secondScreenChoiceEnds[i] + 4);
		}
	}

	// Where each panel goes on the second screen: rows stacked top to
	// bottom, each from its left edge, with panels beside the previous one
	// continuing its row
	Common::Array<Common::Point> origins;
	int rowTop = 0, rowHeight = 0, rowRight = 0;
	for (uint j = 0; j < _secondScreenPanels.size(); j++) {
		const Common::Rect &panel = _secondScreenPanels[j];
		if (j > 0 && j < _secondScreenBeside.size() && _secondScreenBeside[j]) {
			origins.push_back(Common::Point(rowRight + 8, rowTop));
			rowRight += 8 + panel.width();
			rowHeight = MAX<int>(rowHeight, panel.height());
		} else {
			rowTop += rowHeight;
			origins.push_back(Common::Point(0, rowTop));
			rowRight = panel.width();
			rowHeight = panel.height();
		}
	}

	struct Target {
		Common::Rect rect;
		Common::Point center;
		Common::Point click;
		int panel;
	};
	Common::Array<Target> targets;
	for (uint i = 0; i < boxes.size(); i++) {
		Common::Rect r = boxes[i];
		const Common::Point click((r.left + r.right) / 2, (r.top + r.bottom) / 2);

		// Move in the order the second screen shows the panels
		int panelIndex = -1;
		for (uint j = 0; j < _secondScreenPanels.size(); j++) {
			const Common::Rect &panel = _secondScreenPanels[j];
			if (panel.contains(click)) {
				r.translate(origins[j].x - panel.left, origins[j].y - panel.top);
				panelIndex = j;
				break;
			}
		}
		Target t = { r, Common::Point((r.left + r.right) / 2, (r.top + r.bottom) / 2), click, panelIndex };
		targets.push_back(t);
	}

	// Start from the box under the pointer; if there is none, go to the
	// first one in reading order
	const Target *from = nullptr;
	for (uint i = 0; i < targets.size(); i++) {
		if (boxes[i].contains(_mouse))
			from = &targets[i];
	}

	const Target *best = nullptr;
	if (!from) {
		for (uint i = 0; i < targets.size(); i++) {
			const Common::Point &c = targets[i].center;
			if (!best || c.y < best->center.y || (c.y == best->center.y && c.x < best->center.x))
				best = &targets[i];
		}
	} else {
		// The nearest target in the pressed direction. Up and down prefer
		// targets overlapping the current one sideways; left and right stay
		// on the current row, or failing that in the current panel.
		for (int pass = 0; pass < 2 && !best; pass++) {
			int bestScore = 0;
			for (uint i = 0; i < targets.size(); i++) {
				if (&targets[i] == from)
					continue;
				const Common::Rect &r = targets[i].rect;
				const int dx = targets[i].center.x - from->center.x;
				const int dy = targets[i].center.y - from->center.y;
				const int along = dx * dirX + dy * dirY;
				if (along <= 0)
					continue;
				int across;
				if (dirY)
					across = MAX(r.left - from->rect.right, from->rect.left - r.right);
				else
					across = MAX(r.top - from->rect.bottom, from->rect.top - r.bottom);
				if (dirX && across >= 0 && (pass == 0 || targets[i].panel != from->panel))
					continue;
				across = (across < 0) ? 0 : across + 1;
				const int score = along + 3 * across;
				if (!best || score < bestScore) {
					best = &targets[i];
					bestScore = score;
				}
			}
			if (!dirX)
				break;
		}
	}
	if (!best)
		return;

	// The engine reads the pointer from the event manager, so tell it too
	_system->warpMouse(best->click.x, best->click.y);
	Common::Event move;
	move.type = Common::EVENT_MOUSEMOVE;
	move.mouse = best->click;
	_system->getEventManager()->pushEvent(move);
}

} // End of namespace AGOS
