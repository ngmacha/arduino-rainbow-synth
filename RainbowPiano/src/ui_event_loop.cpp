/*********************
 * ui_event_loop.cpp *
 *********************/

/****************************************************************************
 *   Written By Mark Pelletier  2017 - Aleph Objects, Inc.                  *
 *   Written By Marcio Teixeira 2018 - Aleph Objects, Inc.                  *
 *                                                                          *
 *   This program is free software: you can redistribute it and/or modify   *
 *   it under the terms of the GNU General Public License as published by   *
 *   the Free Software Foundation, either version 3 of the License, or      *
 *   (at your option) any later version.                                    *
 *                                                                          *
 *   This program is distributed in the hope that it will be useful,        *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU General Public License for more details.                           *
 *                                                                          *
 *   To view a copy of the GNU General Public License, go to the following  *
 *   location: <http://www.gnu.org/licenses/>.                              *
 ****************************************************************************/

#include "ui.h"

#if ENABLED(EXTENSIBLE_UI)

#include "ftdi_eve_constants.h"
#include "ftdi_eve_functions.h"
#include "ftdi_eve_panels.h"

#include "ui_framework.h"
#include "ui_theme.h"
#include "ui_builder.h"
#include "ui_dl_cache.h"
#include "ui_event_loop.h"
#include "ui_sounds.h"

using namespace FTDI;

enum {
  UNPRESSED       = 0x00
};

tiny_timer_t touch_timer;
UIData::flags_t UIData::flags;
uint8_t pressed_tag  = UNPRESSED;

uint8_t UIData::get_value() {
  return flags.value;
}

void UIData::set_value(uint8_t value) {
  flags_t mask = {true, true, true, true};
  flags.value = value & mask.value;
}

void UIData::enable_touch_sounds(bool enabled) {
  UIData::flags.bits.touch_start_sound  = enabled;
  UIData::flags.bits.touch_end_sound    = enabled;
  UIData::flags.bits.touch_repeat_sound = enabled;
}

bool UIData::touch_sounds_enabled() {
  return UIData::flags.bits.touch_start_sound || UIData::flags.bits.touch_end_sound || UIData::flags.bits.touch_repeat_sound;
}

void UIData::enable_animations(bool enabled) {
    UIData::flags.bits.show_animations = enabled;
}

bool UIData::animations_enabled() {
  return UIData::flags.bits.show_animations;
}

uint8_t get_pressed_tag() {
  return pressed_tag;
}

bool is_touch_held() {
  return pressed_tag != 0;
}

namespace UI {
  void onStartup() {
    using namespace UI;

    CLCD::init();
    DLCache::init();

    current_screen.start();
  }

  void onIdle() {
    sound.onIdle();
    current_screen.onIdle();

    // If the LCD is processing commands, don't check
    // for tags since they may be changing and could
    // cause spurious events.
    if(!touch_timer.elapsed(TOUCH_UPDATE_INTERVAL) || CLCD::CommandFifo::is_processing()) {
      return;
    }

    const uint8_t tag = CLCD::get_tag();

    switch(pressed_tag) {
      case UNPRESSED:
        if(tag != 0) {
          #if defined(UI_FRAMEWORK_DEBUG)
            SERIAL_ECHO_START();
            SERIAL_ECHOLNPAIR("Touch start: ", tag);
          #endif

          pressed_tag = tag;
          current_screen.onRefresh();

          // When the user taps on a button, activate the onTouchStart handler
          const uint8_t lastScreen = current_screen.getScreen();

          if(current_screen.onTouchStart(tag)) {
            touch_timer.start();
            if(UIData::flags.bits.touch_start_sound) sound.play(Theme::press_sound);
          }

          if(lastScreen != current_screen.getScreen()) {
            // In the case in which a touch event triggered a new screen to be
            // drawn, we don't issue a touchEnd since it would be sent to the
            // wrong screen.
            UIData::flags.bits.ignore_unpress = true;
          } else {
            UIData::flags.bits.ignore_unpress = false;
          }
        } else {
          touch_timer.start();
        }
        break;
      default: // PRESSED
        if(!UIData::flags.bits.touch_debouncing) {
          if(tag == pressed_tag) {
            // The user is holding down a button.
            if(touch_timer.elapsed(1000 / TOUCH_REPEATS_PER_SECOND) && current_screen.onTouchHeld(tag)) {
              current_screen.onRefresh();
              if(UIData::flags.bits.touch_repeat_sound) sound.play(Theme::repeat_sound);
              touch_timer.start();
            }
          }
          else if(tag == 0) {
            touch_timer.start();
            UIData::flags.bits.touch_debouncing = true;
          }
        }

        else {
          // Debouncing...

          if(tag == pressed_tag) {
            // If while debouncing, we detect a press, then cancel debouncing.
            UIData::flags.bits.touch_debouncing = false;
          }

          else if(touch_timer.elapsed(DEBOUNCE_PERIOD)) {
            UIData::flags.bits.touch_debouncing = false;

            if(UIData::flags.bits.ignore_unpress) {
              UIData::flags.bits.ignore_unpress = false;
              pressed_tag = UNPRESSED;
              break;
            }

            if(UIData::flags.bits.touch_end_sound) sound.play(Theme::unpress_sound);

            #if defined(UI_FRAMEWORK_DEBUG)
              SERIAL_ECHO_START();
              SERIAL_ECHOLNPAIR("Touch end: ", tag);
            #endif

            const uint8_t saved_pressed_tag = pressed_tag;
            pressed_tag = UNPRESSED;
            current_screen.onTouchEnd(saved_pressed_tag);
            current_screen.onRefresh();
          }
        }
        break;
    } // switch(pressed_tag)

  } // onIdle()

} // UI

#endif // EXTENSIBLE_UI