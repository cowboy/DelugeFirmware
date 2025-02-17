/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once
#include "gui/menu_item/submenu.h"
#include "gui/ui/sound_editor.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::submenu {
class Compressor final : public Submenu {
public:
	Compressor(l10n::String newName, l10n::String title, std::initializer_list<MenuItem*> newItems,
	           bool newForReverbCompressor)
	    : Submenu(newName, title, newItems), forReverbCompressor(newForReverbCompressor) {}
	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override {
		soundEditor.currentCompressor =
		    forReverbCompressor ? &AudioEngine::reverbCompressor : &soundEditor.currentSound->compressor;
		Submenu::beginSession(navigatedBackwardFrom);
	}

	bool forReverbCompressor;
};

} // namespace deluge::gui::menu_item::submenu
