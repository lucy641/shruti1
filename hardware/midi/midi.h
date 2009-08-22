// Copyright 2009 Olivier Gillet. All rights reserved
//
// Author: Olivier Gillet (ol.gillet@gmail.com)
//
// MIDI implementation.

#ifndef HARDWARE_MIDI_MIDI_H_
#define HARDWARE_MIDI_MIDI_H_

namespace hardware_midi {
  
// TODO(oliviergillet): define the other useful message aliases here.
const uint8_t kModulationWheelMsb = 1;
const uint8_t kModulationWheelLsb = 21;

// A device that responds to MIDI messages should implement this interface.
// Everything is static - this is because the main synth class is a "static
// singleton". Note that this allows all the MIDI processing code to be inlined!
struct MidiDevice {
  static void NoteOn(uint8_t channel, uint8_t note, uint8_t velocity) { }
  static void NoteOff(uint8_t channel, uint8_t note, uint8_t velocity) { }
  static void Aftertouch(uint8_t channel, uint8_t note, uint8_t velocity) { }
  static void Aftertouch(uint8_t channel, uint8_t velocity) { }
  static void ControlChange(uint8_t channel, uint8_t controller,
                             uint8_t value) { }
  static void ProgramChange(uint8_t channel, uint8_t program) { }
  static void PitchBend(uint8_t channel, uint16_t pitch_bend) { }
  
  static void AllSoundOff(uint8_t channel) { }
  static void ResetAllControllers(uint8_t channel) { }
  static void LocalControl(uint8_t channel, uint8_t state) { }
  static void AllNotesOff(uint8_t channel) { }
  static void OmniModeOff(uint8_t channel) { }
  static void OmniModeOn(uint8_t channel) { }
  static void MonoModeOn(uint8_t channel, uint8_t num_channels) { }
  static void PolyModeOn(uint8_t channel) { }
  static void SysExStart() { }
  static void SysExByte(uint8_t sysex_byte) { }
  static void SysExEnd() { }
  static void BozoByte(uint8_t bozo_byte) { }
  
  static void Clock() { }
  static void Start() { }
  static void Continue() { }
  static void Stop() { }
  static void ActiveSensing() { }
  static void Reset() { }
};

template<typename Device>
class MidiStreamParser {
 public:
  MidiStreamParser();
  void PushByte(uint8_t byte);

 private:
  void MessageReceived(uint8_t status);
   
  uint8_t running_status_;
  uint8_t data_[3];
  uint8_t data_size_;
  uint8_t expected_data_size_;
  
  DISALLOW_COPY_AND_ASSIGN(MidiStreamParser);
};


template<typename Device>
MidiStreamParser<Device>::MidiStreamParser() {
  running_status_ = 0;
  data_size_ = 0;
  expected_data_size_ = 0;
}

template<typename Device>
void MidiStreamParser<Device>::PushByte(uint8_t byte) {
  // Realtime messages are immediately passed-through, and do not modified the
  // state of the parser.
  if (byte >= 0xf8) {
    MessageReceived(byte);
  } else {
    if (byte >= 0x80) {
      uint8_t hi = byte & 0xf0;
      uint8_t lo = byte & 0x0f;
      data_size_ = 0;
      expected_data_size_ = 1;
      switch (hi) {
        case 0x80:
        case 0x90:
        case 0xa0:
        case 0xb0:
          expected_data_size_ = 2;
          break;
        case 0xc0:
        case 0xd0:
          break;  // default data size of 1.
        case 0xe0:
          expected_data_size_ = 2;
          break;
        case 0xf0:
          if (lo > 0 && lo < 3) {
            expected_data_size_ = 2;
          } else if (lo >= 4) {
            expected_data_size_ = 0;
          }
          break;
      }
      if (running_status_ == 0xf0) {
        Device::SysExEnd();
      }
      running_status_ = byte;
      if (running_status_ == 0xf0) {
        Device::SysExStart();
      }
    } else {
      data_[data_size_++] = byte;
    }
    if (data_size_ >= expected_data_size_) {
      MessageReceived(running_status_);
      data_size_ = 0;
      if (running_status_ > 0xf0) {
        expected_data_size_ = 0;
        running_status_ = 0;
      }
    }
  }
}

template<typename Device>
void MidiStreamParser<Device>::MessageReceived(uint8_t status) {
  if (!status) {
    Device::BozoByte(data_[0]);
  }
  
  uint8_t hi = status & 0xf0;
  uint8_t lo = status & 0x0f;
  switch(hi) {
    case 0x80:
      if (data_[1]) {
        Device::NoteOn(lo, data_[0], data_[1]);
      } else {
        Device::NoteOff(lo, data_[0], 0);
      }
      break;
    case 0x90:
      Device::NoteOff(lo, data_[0], data_[1]);
      break;
    case 0xa0:
      Device::Aftertouch(lo, data_[0], data_[1]);
      break;
    case 0xb0:
      switch (data_[0]) {
        case 0x78:
          Device::AllSoundOff(lo);
          break;
        case 0x79:
          Device::ResetAllControllers(lo);
          break;
        case 0x7a:
          Device::LocalControl(lo, data_[1]);
          break;
        case 0x7b:
          Device::AllNotesOff(lo);
          break;
        case 0x7c:
          Device::OmniModeOff(lo);
          break;
        case 0x7d:
          Device::OmniModeOn(lo);
          break;
        case 0x7e:
          Device::MonoModeOn(lo, data_[1]);
          break;
        case 0x7f:
          Device::PolyModeOn(lo);
          break;
        default:
          Device::ControlChange(lo, data_[0], data_[1]);
          break;
      }
      break;
    case 0xc0:
      Device::ProgramChange(lo, data_[0]);
      break;
    case 0xd0:
      Device::Aftertouch(lo, data_[0]);
      break;
    case 0xe0:
      Device::PitchBend(lo, (uint16_t(data_[0]) << 7) + data_[1]);
      break;
    case 0xf0:
      switch(lo) {
        case 0x0:
          Device::SysExByte(data_[0]);
          break;
        case 0x1:
        case 0x2:
        case 0x3:
        case 0x4:
        case 0x5:
        case 0x6:
          // TODO(oliviergillet): implement this if it makes sense.
          break;
        case 0x7:
          Device::SysExEnd();
          break;
        case 0x8:
          Device::Clock();
          break;
        case 0x9:
          break;
        case 0xa:
          Device::Start();
          break;
        case 0xb:
          Device::Continue();
          break;
        case 0xc:
          Device::Stop();
          break;
        case 0xe:
          Device::ActiveSensing();
          break;
        case 0xf:
          Device::Reset();
          break;
      }
      break;
  }
}

  
}  // namespace hardware_midi

#endif  // HARDWARE_MIDI_MIDI_H_
