/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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

#include "hid/encoders.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/buttons.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "model/action/action_logger.h"
#include "model/settings/runtime_feature_settings.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "util/functions.h"
#include <new>

namespace Encoders {

Encoder encoders[NUM_ENCODERS] = {};
uint32_t timeModEncoderLastTurned[2];
int8_t modEncoderInitialTurnDirection[2];

uint32_t timeNextSDTestAction = 0;
int32_t nextSDTestDirection = 1;

uint32_t encodersWaitingForCardRoutineEnd;

void init() {
	encoders[ENCODER_SCROLL_X].setPins(1, 11, 1, 12);
	encoders[ENCODER_TEMPO].setPins(1, 7, 1, 6);
	encoders[ENCODER_MOD_0].setPins(1, 0, 1, 15);
	encoders[ENCODER_MOD_1].setPins(1, 5, 1, 4);
	encoders[ENCODER_SCROLL_Y].setPins(1, 8, 1, 10);
	encoders[ENCODER_SELECT].setPins(1, 2, 1, 3);

	encoders[ENCODER_MOD_0].setNonDetentMode();
	encoders[ENCODER_MOD_1].setNonDetentMode();
}

void readEncoders() {
	for (int32_t i = 0; i < NUM_ENCODERS; i++) {
		encoders[i].read();
	}
}

extern "C" void readEncoder(int32_t e, int32_t whichPin) {
	encoders[e].interrupt(whichPin);
}

bool interpretEncoders(bool inCardRoutine) {

	bool anything = false;

	if (!inCardRoutine) {
		encodersWaitingForCardRoutineEnd = 0;
	}

#if SD_TEST_MODE_ENABLED
	if (!inCardRoutine && playbackHandler.isEitherClockActive()
	    && (int32_t)(AudioEngine::audioSampleTimer - timeNextSDTestAction) >= 0) {

		if (getRandom255() < 96)
			nextSDTestDirection *= -1;
		getCurrentUI()->selectEncoderAction(nextSDTestDirection);

		int32_t random = getRandom255();

		timeNextSDTestAction = AudioEngine::audioSampleTimer + ((random) << 6); // * 44 / 13;
		anything = true;
	}
#endif

	for (int32_t e = 0; e < NUM_FUNCTION_ENCODERS; e++) {

		if (e != ENCODER_SCROLL_Y) {

			// Basically disables all function encoders during SD routine
			if (inCardRoutine && currentUIMode != UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED) {
				continue;
			}
		}

		if (encodersWaitingForCardRoutineEnd & (1 << e)) {
			continue;
		}

		if (encoders[e].detentPos != 0) {
			anything = true;

			// Limit. Some functions can break if they receive bigger numbers, e.g. LoadSongUI::selectEncoderAction()
			int32_t limitedDetentPos = encoders[e].detentPos;
			encoders[e].detentPos = 0; // Reset. Crucial that this happens before we call selectEncoderAction()
			if (limitedDetentPos >= 0) {
				limitedDetentPos = 1;
			}
			else {
				limitedDetentPos = -1;
			}

			ActionResult result;

			switch (e) {

			case ENCODER_SCROLL_X:
				result = getCurrentUI()->horizontalEncoderAction(limitedDetentPos);
				// Actually, after coding this up, I realise I actually have it above stopping the X encoder from even
				// getting here during the SD routine. Ok so we'll leave it that way, in addition to me having made all
				// the horizontalEncoderAction() calls SD-routine-safe
checkResult:
				if (result == ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE) {
					encodersWaitingForCardRoutineEnd |= (1 << e);
					encoders[e].detentPos = limitedDetentPos; // Put it back for next time
				}
				break;

			case ENCODER_SCROLL_Y:
				if (Buttons::isShiftButtonPressed() && Buttons::isButtonPressed(deluge::hid::button::LEARN)) {
					PadLEDs::changeDimmerInterval(limitedDetentPos);
				}
				else {
					result = getCurrentUI()->verticalEncoderAction(limitedDetentPos, inCardRoutine);
					goto checkResult;
				}
				break;

			case ENCODER_TEMPO:
				if (getCurrentUI() == &instrumentClipView
				    && runtimeFeatureSettings.get(RuntimeFeatureSettingType::Quantize)
				           == RuntimeFeatureStateToggle::On) {
					instrumentClipView.tempoEncoderAction(limitedDetentPos,
					                                      Buttons::isButtonPressed(deluge::hid::button::TEMPO_ENC),
					                                      Buttons::isShiftButtonPressed());
				}
				else {
					playbackHandler.tempoEncoderAction(limitedDetentPos,
					                                   Buttons::isButtonPressed(deluge::hid::button::TEMPO_ENC),
					                                   Buttons::isShiftButtonPressed());
				}
				break;

			case ENCODER_SELECT:
				if (Buttons::isButtonPressed(deluge::hid::button::CLIP_VIEW)) {
					PadLEDs::changeRefreshTime(limitedDetentPos);
				}
				else {
					getCurrentUI()->selectEncoderAction(limitedDetentPos);
				}
				break;
			}
		}
	}

	if (!inCardRoutine || currentUIMode == UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED) {
		// Mod knobs
		for (int32_t e = 0; e < 2; e++) {

			// If encoder turned...
			if (encoders[ENCODER_MOD_0 - e].encPos != 0) {
				anything = true;

				bool turnedRecently = (AudioEngine::audioSampleTimer - timeModEncoderLastTurned[e] < kShortPressTime);

				// If it was turned recently...
				if (turnedRecently) {

					// Mark as turned recently again. Must do this before the encoder-action gets invoked below, because
					// that might want to reset this
					timeModEncoderLastTurned[e] = AudioEngine::audioSampleTimer;

					// Do it, only if
					if (encoders[ENCODER_MOD_0 - e].encPos + modEncoderInitialTurnDirection[e] != 0) {
						getCurrentUI()->modEncoderAction(e, encoders[ENCODER_MOD_0 - e].encPos);
						modEncoderInitialTurnDirection[e] = 0;
					}

					// Otherwise, write this off as an accidental wiggle
					else {
						modEncoderInitialTurnDirection[e] = encoders[ENCODER_MOD_0 - e].encPos;
					}
				}

				// Or if it wasn't turned recently, it's going to get marked as turned recently now, but remember what
				// direction we came, so that if we go back that direction again we can write it off as an accidental
				// wiggle
				else {

					// If the other one also hasn't been turned for a while...
					bool otherTurnedRecently =
					    (AudioEngine::audioSampleTimer - timeModEncoderLastTurned[1 - e] < kShortPressTime);
					if (!otherTurnedRecently) {
						actionLogger.closeAction(ActionType::PARAM_UNAUTOMATED_VALUE_CHANGE);
					}

					modEncoderInitialTurnDirection[e] = encoders[ENCODER_MOD_0 - e].encPos;

					// Mark as turned recently
					timeModEncoderLastTurned[e] = AudioEngine::audioSampleTimer;
				}

				encoders[ENCODER_MOD_0 - e].encPos = 0;
			}
		}
	}

	return anything;
}

} // namespace Encoders
