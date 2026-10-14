// Experimental read-only X10A diagnostics. No write commands.
#include "altherma_hub.h"
#include "mock_uart.h"
#include "x10a_read_protocol.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace esphome {
namespace altherma_hub {

bool AlthermaHub::reserve_diagnostic_(DiagnosticKind kind) {
  if (diagnostic_pending_ || diagnostic_active_) {
    publish_manual_query_statusf_(ESPHOME_LOG_LEVEL_WARN, "X10A diagnostic already queued/running");
    return false;
  }
  diagnostic_kind_ = kind;
  diagnostic_received_ = 0;
  diagnostic_pending_ = true;
  return true;
}

void AlthermaHub::queue_software_id() {
  if (enable_memory_diagnostics_ && reserve_diagnostic_(DiagnosticKind::SOFTWARE_ID))
    publish_manual_query_statusf_(ESPHOME_LOG_LEVEL_INFO, "X10A software ID read queued");
}

void AlthermaHub::queue_read_memory(std::string space, std::string address, int32_t length) {
  if (!enable_memory_diagnostics_) {
    publish_manual_query_statusf_(ESPHOME_LOG_LEVEL_WARN, "X10A memory diagnostics disabled");
    return;
  }
  const bool ram = space == "ram";
  if (!ram && space != "memory") {
    publish_manual_query_statusf_(ESPHOME_LOG_LEVEL_ERROR, "X10A: space must be ram or memory");
    return;
  }
  errno = 0;
  char *end = nullptr;
  const long parsed = std::strtol(address.c_str(), &end, 0);
  if (end == address.c_str() || *end != '\0' || errno != 0 || parsed < 0 ||
      parsed > 0xFFFF || length < 1 || length > 64 ||
      !x10a::valid_read_range(static_cast<uint16_t>(parsed), static_cast<uint16_t>(length), ram)) {
    publish_manual_query_statusf_(ESPHOME_LOG_LEVEL_ERROR,
                                  "X10A: invalid memory address/length (1..64 bytes)");
    return;
  }
  if (!reserve_diagnostic_(ram ? DiagnosticKind::RAM : DiagnosticKind::MEMORY)) return;
  diagnostic_address_ = static_cast<uint16_t>(parsed);
  diagnostic_length_ = static_cast<uint16_t>(length);
  publish_manual_query_statusf_(ESPHOME_LOG_LEVEL_INFO, "X10A %s at 0x%04lX, %d bytes queued",
                                space.c_str(), parsed, length);
}

void AlthermaHub::queue_field_setting(std::string code) {
  if (!enable_experimental_field_settings_) {
    publish_manual_query_statusf_(ESPHOME_LOG_LEVEL_WARN, "X10A field reads disabled");
    return;
  }
  uint8_t page = 0, setting = 0;
  if (!x10a::parse_field_code(code, page, setting)) {
    publish_manual_query_statusf_(ESPHOME_LOG_LEVEL_ERROR, "X10A: invalid field code, e.g. [1-02]");
    return;
  }
  if (!reserve_diagnostic_(DiagnosticKind::FIELD_SETTING)) return;
  diagnostic_page_ = page;
  diagnostic_setting_ = setting;
  publish_manual_query_statusf_(ESPHOME_LOG_LEVEL_INFO,
                                "X10A field [%X-%02X] queued (experimental; raw reply)",
                                page, setting);
}

void AlthermaHub::finish_diagnostic_() {
  diagnostic_active_ = false;
  diagnostic_kind_ = DiagnosticKind::NONE;
  poll_active_ = false;
  query_state_ = QueryState::IDLE;
  rx_len_ = 0;
}

void AlthermaHub::start_diagnostic_query_() {
  auto *uart = parent_;
#ifdef USE_MOCK_UART
  uart = &shared_mock_uart();
#endif
  uint8_t args[6] = {};
  size_t n = 0;
  uint8_t cmd = 0;
  switch (diagnostic_kind_) {
    case DiagnosticKind::SOFTWARE_ID:
      cmd = 0x4C;
      diagnostic_expected_total_ = 6;
      break;
    case DiagnosticKind::RAM:
    case DiagnosticKind::MEMORY: {
      const uint16_t addr = static_cast<uint16_t>(diagnostic_address_ + diagnostic_received_);
      args[0] = static_cast<uint8_t>(addr);
      args[1] = static_cast<uint8_t>(addr >> 8);
      n = 2;
      cmd = diagnostic_kind_ == DiagnosticKind::RAM ? 0x52 : 0x49;
      diagnostic_expected_total_ = cmd == 0x52 ? 18 : 3;
      break;
    }
    case DiagnosticKind::FIELD_SETTING:
      cmd = 0x21;
      args[0] = 0x49;
      args[1] = 0;
      args[2] = 1;
      args[3] = 1;
      args[4] = diagnostic_page_;
      args[5] = diagnostic_setting_;
      n = 6;
      diagnostic_expected_total_ = 0; // unknown response format
      break;
    default:
      finish_diagnostic_();
      return;
  }
  uint8_t frame[12] = {};
  const size_t count = x10a::build_frame(cmd, args, n, frame, sizeof(frame));
  if (!count) {
    publish_manual_query_statusf_(ESPHOME_LOG_LEVEL_ERROR, "X10A: invalid read frame");
    finish_diagnostic_();
    return;
  }
  diagnostic_command_ = cmd;
  rx_len_ = 0;
  query_started_at_ = millis();
  diagnostic_last_byte_at_ = query_started_at_;
  uint8_t junk = 0;
  while (uart->available() && uart->read_byte(&junk)) {}
  uart->flush();
  uart->write_array(frame, count);
  query_state_ = QueryState::READ_RESPONSE;
}

void AlthermaHub::read_diagnostic_response_() {
  auto *uart = parent_;
#ifdef USE_MOCK_UART
  uart = &shared_mock_uart();
#endif
  const uint32_t now = millis();
  const size_t max_len = diagnostic_expected_total_ ? diagnostic_expected_total_ : RX_BUFFER_SIZE;
  const size_t count = std::min(static_cast<size_t>(uart->available()), max_len - rx_len_);
  if (count) {
    uint8_t byte = 0;
    if (uart->read_array(rx_buffer_ + rx_len_, count)) {
      rx_len_ += count;
      diagnostic_last_byte_at_ = now;
    } else if (uart->read_byte(&byte)) {
      rx_buffer_[rx_len_++] = byte;
      diagnostic_last_byte_at_ = now;
    }
  }
  if (rx_len_ == 2 && x10a::nack(rx_buffer_, rx_len_)) {
    publish_manual_query_statusf_(ESPHOME_LOG_LEVEL_WARN, "X10A cmd 0x%02X NACK (15 EA)",
                                  diagnostic_command_);
    finish_diagnostic_();
    return;
  }
  if (diagnostic_kind_ == DiagnosticKind::FIELD_SETTING) {
    if (rx_len_ == RX_BUFFER_SIZE ||
        (rx_len_ > 0 && now - diagnostic_last_byte_at_ >= DIAGNOSTIC_FIELD_GAP_MS)) {
      report_field_setting_response_(rx_len_ == RX_BUFFER_SIZE);
      finish_diagnostic_();
    } else if (now - query_started_at_ >= DIAGNOSTIC_FIELD_TIMEOUT_MS) {
      if (rx_len_ > 0) report_field_setting_response_(true);
      else publish_manual_query_statusf_(ESPHOME_LOG_LEVEL_WARN,
                                         "X10A field [%X-%02X] timeout/no response",
                                         diagnostic_page_, diagnostic_setting_);
      finish_diagnostic_();
    }
    return;
  }
  if (rx_len_ >= diagnostic_expected_total_) {
    if (!x10a::valid_reply(rx_buffer_, rx_len_, diagnostic_command_)) {
      publish_manual_query_statusf_(ESPHOME_LOG_LEVEL_ERROR,
                                    "X10A 0x%02X invalid reply/CRC", diagnostic_command_);
      finish_diagnostic_();
      return;
    }
    if (diagnostic_kind_ == DiagnosticKind::SOFTWARE_ID) {
      publish_manual_query_statusf_(ESPHOME_LOG_LEVEL_INFO, "X10A hydro software ID=%02X%02X%02X%02X",
                                    rx_buffer_[4], rx_buffer_[3], rx_buffer_[2], rx_buffer_[1]);
      finish_diagnostic_();
      return;
    }
    size_t n = diagnostic_kind_ == DiagnosticKind::RAM ? 16 : 1;
    n = std::min(n, static_cast<size_t>(diagnostic_length_ - diagnostic_received_));
    std::memcpy(diagnostic_data_ + diagnostic_received_, rx_buffer_ + 1, n);
    diagnostic_received_ += n;
    if (diagnostic_received_ < diagnostic_length_) {
      query_state_ = QueryState::SEND_QUERY;
      return;
    }
    char hex[64 * 3 + 1] = {};
    size_t pos = 0;
    for (size_t j = 0; j < diagnostic_received_; j++) {
      const int written = std::snprintf(hex + pos, sizeof(hex) - pos, "%s%02X",
                                         j == 0 ? "" : " ", diagnostic_data_[j]);
      if (written <= 0) break;
      pos += static_cast<size_t>(written);
    }
    publish_manual_query_statusf_(ESPHOME_LOG_LEVEL_INFO, "X10A %s 0x%04X n=%u: %s",
                                  diagnostic_kind_ == DiagnosticKind::RAM ? "ram" : "memory",
                                  diagnostic_address_, diagnostic_length_, hex);
    finish_diagnostic_();
    return;
  }
  if (now - query_started_at_ >= DIAGNOSTIC_READ_TIMEOUT_MS) {
    publish_manual_query_statusf_(ESPHOME_LOG_LEVEL_ERROR, "X10A 0x%02X timeout (%u/%u bytes)",
                                  diagnostic_command_, static_cast<unsigned>(rx_len_),
                                  static_cast<unsigned>(diagnostic_expected_total_));
    finish_diagnostic_();
  }
}

void AlthermaHub::report_field_setting_response_(bool partial) {
  char hex[RX_BUFFER_SIZE * 3 + 1] = {};
  size_t pos = 0;
  for (size_t j = 0; j < rx_len_; j++) {
    const int written = std::snprintf(hex + pos, sizeof(hex) - pos, "%s%02X",
                                       j == 0 ? "" : " ", rx_buffer_[j]);
    if (written <= 0) break;
    pos += static_cast<size_t>(written);
  }
  const bool crc_ok = rx_len_ >= 2 &&
      x10a::checksum(rx_buffer_, rx_len_ - 1) == rx_buffer_[rx_len_ - 1];
  publish_manual_query_statusf_(partial ? ESPHOME_LOG_LEVEL_WARN : ESPHOME_LOG_LEVEL_INFO,
                                "X10A field [%X-%02X] %s CRC=%s raw=%s",
                                diagnostic_page_, diagnostic_setting_, partial ? "partial" : "raw",
                                crc_ok ? "ok" : "unknown", hex);
}

} // namespace altherma_hub
} // namespace esphome
