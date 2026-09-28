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

bool AGOSEngine::getInterfaceBoxes(Common::Array<Common::Rect> &boxes, bool &dialog) const {
	// Simon 2 sometimes uses the whole screen for the room
	if (getGameType() == GType_SIMON2 && const_cast<AGOSEngine *>(this)->getBitFlag(79))
		return false;

	const Common::Rect area(0, kInterfaceTop, _screenWidth, _screenHeight);
	auto inArea = [&](const HitArea &ha) {
		return Common::Rect(ha.x, ha.y, ha.x + ha.width, ha.y + ha.height).findIntersectingRect(area);
	};

	// The save and load dialog covers the whole screen. Its file slots are
	// only enabled while it is open.
	bool verbs = false;
	dialog = false;
	for (uint i = 0; i < ARRAYSIZE(_hitAreas); i++) {
		const HitArea &ha = _hitAreas[i];
		if (!(ha.flags & kBFBoxInUse) || (ha.flags & kBFBoxDead))
			continue;
		if (ha.id >= 208 && ha.id <= 213)
			return false;
		if (inArea(ha).isEmpty())
			continue;
		if (ha.id >= 101 && ha.id <= 112)
			verbs = true;
		if (ha.flags & kBFTextBox)
			dialog = true;
	}

	// Without verbs or choices, the interface is not showing, e.g. in the
	// intro, and there is nothing to lay out
	if (!verbs && !dialog)
		return true;

	for (uint i = 0; i < ARRAYSIZE(_hitAreas); i++) {
		const HitArea &ha = _hitAreas[i];
		if (!(ha.flags & kBFBoxInUse) || (ha.flags & kBFBoxDead) || (ha.id >= 200 && ha.id <= 213))
			continue;
		// During conversations, only the choices can be picked
		if (dialog && !(ha.flags & kBFTextBox))
			continue;
		const Common::Rect r = inArea(ha);
		// Boxes spanning most of the interface, such as the sentence line,
		// are neither verbs nor items
		if (!dialog && r.width() >= _screenWidth * 3 / 4)
			continue;
		if (!r.isEmpty())
			boxes.push_back(r);
	}
	return true;
}

void AGOSEngine::updateSecondScreenLayout() {
	if (!usesSecondScreen()) {
		if (!_secondScreenPanels.empty()) {
			_secondScreenPanels.clear();
			_system->setSecondScreenLayout(_secondScreenPanels);
		}
		return;
	}

	const Common::Rect area(0, kInterfaceTop, _screenWidth, _screenHeight);
	Common::Array<Common::Rect> boxes;
	bool dialog = false;
	Common::Array<Common::Rect> panels;
	if (!getInterfaceBoxes(boxes, dialog)) {
		// Show the whole game on the main screen
	} else if (boxes.empty()) {
		// Nothing to pick (e.g. a cutscene): keep the layout we had, so the
		// second screen does not jump around. Before the interface first
		// appears (e.g. the intro), show the whole game on the main screen.
		return;
	} else if (dialog) {
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
		Common::Array<bool> used(_screenWidth, false);
		Common::Array<bool> straddled(_screenWidth, false);
		for (uint i = 0; i < boxes.size(); i++) {
			const Common::Rect &r = boxes[i];
			boxesTop = MIN<int>(boxesTop, r.top);
			for (int x = r.left; x < r.right; x++) {
				used[x] = true;
				if (x > r.left)
					straddled[x] = true;
			}
		}

		// The sentence line sits above the verbs and spans the full width
		if (boxesTop - area.top >= 4)
			panels.push_back(Common::Rect(0, area.top, _screenWidth, boxesTop));
		else
			boxesTop = area.top;

		// Split the verbs from the inventory in the empty columns closest to
		// the middle, or between two touching boxes if there are none
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

	if (panels == _secondScreenPanels)
		return;
	_secondScreenPanels = panels;
	_system->setSecondScreenLayout(_secondScreenPanels);
}

void AGOSEngine::focusNextBox(int dirX, int dirY) {
	Common::Array<Common::Rect> boxes;
	bool dialog = false;
	if (!usesSecondScreen() || !getInterfaceBoxes(boxes, dialog) || boxes.empty())
		return;

	struct Target {
		Common::Rect rect;
		Common::Point center;
		Common::Point click;
	};
	Common::Array<Target> targets;
	for (uint i = 0; i < boxes.size(); i++) {
		Common::Rect r = boxes[i];
		const Common::Point click((r.left + r.right) / 2, (r.top + r.bottom) / 2);

		// Move in the order the second screen shows the panels: stacked top
		// to bottom, each from its left edge
		int stackTop = 0;
		for (uint j = 0; j < _secondScreenPanels.size(); j++) {
			const Common::Rect &panel = _secondScreenPanels[j];
			if (panel.contains(click)) {
				r.translate(-panel.left, stackTop - panel.top);
				break;
			}
			stackTop += panel.height();
		}
		Target t = { r, Common::Point((r.left + r.right) / 2, (r.top + r.bottom) / 2), click };
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
		// on the current row.
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
			if (dirX && across >= 0)
				continue;
			across = (across < 0) ? 0 : across + 1;
			const int score = along + 3 * across;
			if (!best || score < bestScore) {
				best = &targets[i];
				bestScore = score;
			}
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
