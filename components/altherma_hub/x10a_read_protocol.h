#pragma once

// Read-only X10A framing helpers. No ESPHome or Arduino dependency.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace esphome {
namespace altherma_hub {
namespace x10a {

inline uint8_t checksum(const uint8_t *bytes, size_t length) {
  uint8_t sum = 0;
  for (size_t i = 0; i < length; i++) sum = static_cast<uint8_t>(sum + bytes[i]);
  return static_cast<uint8_t>(~sum);
}

// Request: [length excluding first byte and CRC][command][data...][CRC].
inline size_t build_frame(uint8_t command, const uint8_t *data, size_t data_length,
                          uint8_t *frame, size_t capacity) {
  if (frame == nullptr || data_length > 252 || capacity < data_length + 3 ||
      (data_length != 0 && data == nullptr)) return 0;
  frame[0] = static_cast<uint8_t>(data_length + 2);
  frame[1] = command;
  if (data_length != 0) std::memcpy(frame + 2, data, data_length);
  frame[data_length + 2] = checksum(frame, data_length + 2);
  return data_length + 3;
}

// Controller replies do not start with a length byte.
inline bool valid_reply(const uint8_t *reply, size_t length, uint8_t command) {
  return reply != nullptr && length >= 2 && reply[0] == command &&
         checksum(reply, length - 1) == reply[length - 1];
}

// Daikin Configurator auto-detect (V command): response is
// [0x56, low byte of Micon ID, high byte of Micon ID, checksum].
// It is a controller ID, not a unique physical model or software ID.
inline bool decode_micon_id(const uint8_t *reply, size_t length, uint16_t &micon_id) {
  if (length != 4 || !valid_reply(reply, length, 0x56)) return false;
  micon_id = static_cast<uint16_t>(reply[1]) |
             (static_cast<uint16_t>(reply[2]) << 8);
  return true;
}

inline bool nack(const uint8_t *reply, size_t length) {
  return reply != nullptr && length == 2 && reply[0] == 0x15 && reply[1] == 0xEA;
}

inline int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Daikin manual field codes look like [1-02] or 1-02, not physical addresses.
inline bool parse_field_code(const std::string &input, uint8_t &page, uint8_t &setting) {
  std::string code = input;
  if (code.size() == 6 && code[0] == '[' && code[5] == ']')
    code = code.substr(1, 4);
  if (code.size() != 4 || code[1] != '-') return false;
  int p = hex_digit(code[0]), hi = hex_digit(code[2]), lo = hex_digit(code[3]);
  if (p < 0 || hi < 0 || lo < 0) return false;
  int s = hi * 16 + lo;
  if (s > 0x0E) return false;  // The documented overview uses settings 00..0E.
  page = static_cast<uint8_t>(p);
  setting = static_cast<uint8_t>(s);
  return true;
}

// RAM command 0x52 always fetches a complete 16-byte window.
inline bool valid_read_range(uint16_t address, uint16_t length, bool ram) {
  if (length == 0 || length > 64) return false;
  uint32_t last = static_cast<uint32_t>(address) + length - 1;
  if (last > 0xFFFF) return false;
  if (ram) {
    const uint32_t last_window_start = static_cast<uint32_t>(address) + ((length - 1) / 16) * 16;
    return last_window_start + 15 <= 0xFFFF;
  }
  return true;
}

}  // namespace x10a
}  // namespace altherma_hub
}  // namespace esphome
