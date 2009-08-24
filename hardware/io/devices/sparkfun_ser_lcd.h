// Copyright 2009 Olivier Gillet. All rights reserved
//
// Author: Olivier Gillet (ol.gillet@gmail.com)
// 
// Driver for a 2x16 LCD display, with double buffering. All updates to the
// content of the screen are done in an in-memory "local" text page. A "remote"
// text page mirrors the current state of the LCD display. A timer (the same as
// for the audio rendering) periodically scans the local and remote pages for
// differences, transmit serially the modified character in the local page to
// the LCD, and update the remote buffer to reflect that the character was
// transmitted.

#ifndef HARDWARE_IO_DISPLAY_H_
#define HARDWARE_IO_DISPLAY_H_

#include "hardware/base/base.h"
#include "hardware/io/log2.h"
#include "hardware/io/software_serial.h"
#include "hardware/resources/resources_manager.h"
#include "hardware/utils/logging.h"

using hardware_resources::SimpleResourcesManager;

namespace hardware_io {

static const uint8_t kLcdNoCursor = 0xff;
static const uint8_t kLcdCursorBlinkRate = 0x7f;
static const uint8_t kLcdCursor = 0xff;

template<typename TxPin,
         uint16_t main_timer_rate,
         uint16_t baud_rate,
         uint8_t width = 16,
         uint8_t height = 2>
class Display {
 public:
  enum {
    lcd_buffer_size = width * height,
    lcd_buffer_size_wrap = width * height - 1
  };
  typedef BufferedSoftwareSerialOutput<
      TxPin,
      main_timer_rate,
      baud_rate,
      8> DisplaySerialOutput;

  typedef SoftwareSerialOutput<
      TxPin,
      9600> DisplayPanicSerialOutput;

  Display() { }
  static void Init() {
    memset(local_, ' ', lcd_buffer_size);
    memset(remote_, '?', lcd_buffer_size);
    scan_position_last_write_ = 255;
    blink_ = 0;
    cursor_position_ = 255;
    if (baud_rate == 2400) {
      DisplayPanicSerialOutput::Write(124);
      DisplayPanicSerialOutput::Write(11);
    }
    DisplaySerialOutput::Init();
  }
  
  static void Print(uint8_t line, const char* text) {
    if (line == 0) {
      LOG(INFO) << "display\ttext\t+----------------+";
    }
    LOG(INFO) << "display\ttext\t|" << text << "|";
    if (line == 1) {
      LOG(INFO) << "display\ttext\t+----------------+";
    }
    uint8_t row = width;
    uint8_t* destination = local_ + (line << Log2<width>::value);
    while (*text && row) {
      uint8_t character = *text;
      // Do not write control characters.
      if (character == 124 || character == 254 ||
          (character >= 8 && character < 32)) {
        *destination++ = ' ';
      } else {
        *destination++ = character;
      }
      ++text;
      --row;
    }
  }
  
  static void SetBrightness(uint8_t brightness) {  // 0 to 29.
    WriteCommand(0x7c, 128 + brightness);
  }

  static void SetCustomCharMap(const uint8_t* characters,
                               uint8_t num_characters) {
   WriteCommand(0xfe, 0x01);
   for (uint8_t i = 0; i < num_characters; ++i) {
     WriteCommand(0xfe, 0x40 + i * 8);
     for (uint8_t j = 0; j < 8; ++j) {
       // The 6th bit is not used, so it is set to prevent character definition
       // data to be misunderstood with special commands.
       WriteCommand(0, 0x20 |  SimpleResourcesManager::Lookup<uint8_t, uint8_t>(
           characters, i * 8 + j));
     }
     WriteCommand(0xfe, 0x01);
   }                               
  }

  // Use kLcdNoCursor (255) or any other value outside of the screen to hide.
  static inline void set_cursor_position(uint8_t cursor) {
    cursor_position_ = cursor;
  }

  static inline void set_status(uint8_t status) {
    // TODO(oliviergillet): we're using the same clock for blinking the cursor
    // and the status indicator. ewwww...
    blink_clock_ = 0;
    status_ = status + 1;
    // Make sure that the next character to be redrawn will be the status char.
    scan_position_ = local_[0] == ' ' ? 0 : (width - 1);
  }

  static inline void Tick() { DisplaySerialOutput::Tick(); }
  static void Update() {
    // The following code is likely to write 3 bytes at most. If there are less
    // than 3 bytes available for write in the output buffer, there's no reason
    // to take the risk to continue.
    if (DisplaySerialOutput::writable() < 3) {
      return;
    }
    // It is now safe that all write of 3 bytes to the display buffer will not
    // block.

    blink_clock_ = (blink_clock_ + 1) & kLcdCursorBlinkRate;
    if (blink_clock_ == 0) {
      blink_ = ~blink_;
      status_ = 0;
    }

    uint8_t character = 0;
    // Determine which character to show at the current position.
    // If the scan position is the cursor and it is shown (blinking), draw the
    // cursor.
    if (scan_position_ == cursor_position_ && blink_) {
      character = kLcdCursor;
    } else {
      // Otherwise, check if there's a status indicator to display. It is
      // displayed either on the left or right of the first line, depending on
      // the available space.
      if (status_ && (scan_position_ == 0 || scan_position_ == (width - 1)) &&
          local_[scan_position_] == ' ') {
        character = status_ - 1;
      } else {
        character = local_[scan_position_];
      }
    }
    // TODO(oliviergillet): check if we can get rid of the
    // scan_position_ == cursor_position_ condition (dead code?).
    if (character != remote_[scan_position_] ||
        scan_position_ == cursor_position_) {
      // There is a character to transmit!
      // If the new character to transmit is just after the previous one, and on
      // the same line, we're good, we don't need to reposition the cursor.
      if ((scan_position_ == scan_position_last_write_ + 1) &&
          (scan_position_ & (width - 1))) {
        // We use overwrite because we have checked before that there is
        // enough room in the buffer.
        DisplaySerialOutput::Overwrite(character);
      } else {
        // The character to transmit is at a different position, we need to move
        // the cursor, and compute the cursor move command argument.
        uint8_t cursor_position = 0x80;
        cursor_position |= (scan_position_ & ~(width - 1)) <<
            Log2<64 / width>::value;
        cursor_position |= (scan_position_ & (width - 1));
        DisplaySerialOutput::Overwrite(0xfe);
        DisplaySerialOutput::Overwrite(cursor_position);
        DisplaySerialOutput::Overwrite(character);
      }
      // We can now assume that remote display will be updated. This works because
      // the entry to this block of code is protected by a check on the
      // transmission success!
      remote_[scan_position_] = character;
      scan_position_last_write_ = scan_position_;
    }
    scan_position_ = (scan_position_ + 1) & lcd_buffer_size_wrap;
  }

 private:
  // Writes a pair of related bytes (command/argument). The main purpose of
  // this function is simply to avoid the write function to be inlined too
  // often.
  static void WriteCommand(uint8_t command, uint8_t argument) {
    if (command) {
      DisplaySerialOutput::Write(command);
    }
    DisplaySerialOutput::Write(argument);
  }
  // Character pages storing what the display currently shows (remote), and
  // what it ought to show (local).
  static uint8_t local_[width * height];
  static uint8_t remote_[width * height];

  // Position of the last character being transmitted.
  static uint8_t scan_position_;
  static uint8_t scan_position_last_write_;
  static uint8_t blink_;
  static uint8_t blink_clock_;
  static uint8_t cursor_position_;
  static uint8_t status_;

  DISALLOW_COPY_AND_ASSIGN(Display);
};

template<typename TxPin, uint16_t main_timer_rate, uint16_t baud_rate,
         uint8_t width, uint8_t height>
uint8_t Display<TxPin, main_timer_rate, baud_rate, width,
                height>::local_[width * height];

template<typename TxPin, uint16_t main_timer_rate, uint16_t baud_rate,
         uint8_t width, uint8_t height>
uint8_t Display<TxPin, main_timer_rate, baud_rate, width,
                height>::remote_[width * height];

template<typename TxPin, uint16_t main_timer_rate, uint16_t baud_rate,
         uint8_t width, uint8_t height>
uint8_t Display<TxPin, main_timer_rate, baud_rate, width,
                height>::scan_position_;

template<typename TxPin, uint16_t main_timer_rate, uint16_t baud_rate,
         uint8_t width, uint8_t height>
uint8_t Display<TxPin, main_timer_rate, baud_rate, width,
                height>::scan_position_last_write_;

template<typename TxPin, uint16_t main_timer_rate, uint16_t baud_rate,
         uint8_t width, uint8_t height>
uint8_t Display<TxPin, main_timer_rate, baud_rate, width, height>::blink_;

template<typename TxPin, uint16_t main_timer_rate, uint16_t baud_rate,
         uint8_t width, uint8_t height>
uint8_t Display<TxPin, main_timer_rate, baud_rate, width, height>::blink_clock_;

template<typename TxPin, uint16_t main_timer_rate, uint16_t baud_rate,
         uint8_t width, uint8_t height>
uint8_t Display<TxPin, main_timer_rate, baud_rate, width,
                height>::cursor_position_;

template<typename TxPin, uint16_t main_timer_rate, uint16_t baud_rate,
         uint8_t width, uint8_t height>
uint8_t Display<TxPin, main_timer_rate, baud_rate, width, height>::status_;

}  // namespace hardware_io

#endif   // HARDWARE_IO_DISPLAY_H_