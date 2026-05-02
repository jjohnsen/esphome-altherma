#include "altherma_hub.h"
#include "esphome/core/log.h"
#include "mock_uart.h"
#include "labeldef.h"
#include <cstdarg>
#include <cstdio>
LabelDef labelDefs[] = {};

//namespace espaltherma {

  // Work arounds to satisfy converters.h
  static const char *const CONV_TAG = "altherma_conv";
  struct FakeSerial {
    void print(const char *msg) {
      ESP_LOGV(CONV_TAG, "%s", msg);
    }

    template<typename... Args>
    void printf(const char *fmt, Args... args) {
      ESP_LOGV(CONV_TAG, fmt, args...);
    }  
  };
  static FakeSerial Serial;

//}
namespace esphome {
namespace altherma_hub {
  #include "converters.h"

static const char *TAG = "altherma_hub";

void AlthermaHub::register_sensor(AlthermaSensorBase *sensor) {
  this->sensors_.push_back(sensor);

  if (std::find(this->registers_.begin(), this->registers_.end(),
                sensor->registry_id()) == this->registers_.end()) {
    this->registers_.push_back(sensor->registry_id());
  }
}

void AlthermaHub::setup() {
  ESP_LOGI(TAG, "Altherma hub setup");
  this->converter_ = new Converter();
  
  this->register_service(&AlthermaHub::queue_manual_query, "query_register",
                         {"register", "offset", "convid", "datasize"});  
}

AlthermaHub::~AlthermaHub() {
  delete this->converter_;
}

void AlthermaHub::publish_manual_query_statusf_(esp_log_level_t level, const char *fmt, ...) {
  char message[384];
  va_list args;
  va_start(args, fmt);
  vsnprintf(message, sizeof(message), fmt, args);
  va_end(args);

  esp_log_write(level, TAG, "%s", message);

  if (this->query_result_text_sensor_ != nullptr) {
    this->query_result_text_sensor_->publish_state(message);
  }
}

void AlthermaHub::update() {
  if (this->registers_.empty()) {
    ESP_LOGV(TAG, "No registers configured");
    return;
  }

  if (this->poll_active_) {
    ESP_LOGW(TAG, "Previous poll cycle still active, skipping tick");
    return;
  }

  ESP_LOGD(TAG, "Update start");
  this->cycle_started_at_ = millis();
  this->begin_poll_cycle_();
}

void AlthermaHub::loop() {
  if (!this->poll_active_ && !this->manual_query_pending_) {
    return;
  }

  if (!this->poll_active_ && this->manual_query_pending_) {
    this->poll_active_ = true;
    this->manual_query_active_ = true;
    this->query_state_ = QueryState::SEND_QUERY;
    this->cycle_started_at_ = millis();
  }

  switch (this->query_state_) {
    case QueryState::SEND_QUERY:
      if (this->manual_query_active_) {
        this->manual_query_pending_ = false;
        this->start_query_(this->manual_register_);
      } else {
        this->start_query_(this->registers_[this->register_index_]);
      }
      break;
    case QueryState::READ_RESPONSE:
      this->read_response_();
      break;
    case QueryState::IDLE:
    default:
      break;
  }
}

void AlthermaHub::queue_manual_query(std::string registry_id, int32_t offset, int32_t convid, int32_t datasize) {
  if (this->manual_query_active_ || this->manual_query_pending_) {
    this->publish_manual_query_statusf_(ESP_LOG_WARN,
                                        "Manual query rejected: previous manual query still in progress");
    return;
  }

  char *end = nullptr;
  errno = 0;
  
  long value = std::strtol(registry_id.c_str(), &end, 0);  // base 0: auto-detect 0x, decimal, octal
  if (end == registry_id.c_str() || *end != '\0' || errno != 0) {
    this->publish_manual_query_statusf_(ESP_LOG_ERROR,
                                        "Manual query rejected: invalid register: %s", registry_id.c_str());
    return;
  }

  if (value < 0 || value > 0xFF) {
    this->publish_manual_query_statusf_(ESP_LOG_ERROR,
                                        "Manual query rejected: register out of range: %ld", value);
    return;
  }

  if (offset < 0 || datasize <= 0) {
    this->publish_manual_query_statusf_(ESP_LOG_ERROR,
                                        "Manual query rejected: invalid bounds offset=%d datasize=%d", offset, datasize);
    return;
  }

  this->manual_register_ = static_cast<uint8_t>(value);
  this->manual_offset_ = offset;
  this->manual_convid_ = convid;
  this->manual_datasize_ = datasize;
  this->manual_query_pending_ = true;

  this->publish_manual_query_statusf_(ESP_LOG_INFO,
                                      "Manual query accepted reg=0x%02lX offset=%d convid=%d datasize=%d",
                                      value, offset, convid, datasize);

  if (!this->poll_active_) {
    this->poll_active_ = true;
    this->manual_query_active_ = true;
    this->query_state_ = QueryState::SEND_QUERY;
    this->cycle_started_at_ = millis();
  }
}

bool AlthermaHub::decode_label(AlthermaSensorBase *sensor, unsigned char *frame, size_t frame_len, LabelDef &out) {
  if (frame_len < 4) {
    ESP_LOGE(TAG, "Frame too short for register 0x%02x: len=%d", sensor->registry_id(), frame_len);
    return false;
  }

  const int offset = sensor->offset();
  const int data_size = sensor->datasize();
  if (offset < 0 || data_size <= 0) {
    ESP_LOGE(TAG, "Invalid sensor bounds for '%s': offset=%d size=%d", sensor->get_name().c_str(), offset, data_size);
    return false;
  }

  const size_t payload_start = static_cast<size_t>(offset) + 3;
  const size_t payload_end = payload_start + static_cast<size_t>(data_size);
  const size_t frame_data_end = frame_len - 1;  // exclude CRC byte
  if (payload_start >= frame_data_end || payload_end > frame_data_end) {
    ESP_LOGE(TAG,
             "Out-of-bounds decode for '%s' reg=0x%02x offset=%d size=%d frame_len=%d",
             sensor->get_name().c_str(), sensor->registry_id(), offset, data_size, frame_len);
    return false;
  }

  out = LabelDef(
    sensor->registry_id(),
    offset,
    sensor->convid(),
    data_size,
    1,
    sensor->get_name().c_str()
  );

  unsigned char *input = frame + payload_start;
  this->converter_->convert(&out, input);

  return true;
}

void AlthermaHub::begin_poll_cycle_() {
  this->poll_active_ = true;
  this->register_index_ = 0;
  this->query_state_ = QueryState::SEND_QUERY;
}

void AlthermaHub::start_query_(uint8_t regID) {
  auto uart = this->parent_;
#ifdef USE_MOCK_UART
  ESP_LOGW(TAG, "Using MockUART");
  static MockUART mock_uart;
  uart = &mock_uart;
#endif

  uint8_t command[] = {0x03, 0x40, regID, 0x00};
  command[3] = calculate_crc(command, 3);

  this->current_register_ = regID;
  this->rx_len_ = 0;
  this->expected_total_ = 12;  // until byte[2] (length) is known
  this->query_started_at_ = millis();

  ESP_LOGV(TAG, "Querying register 0x%02x... CRC: 0x%02x", regID, command[3]);
  uart->flush();
  uart->write_array(command, 4);
  this->query_state_ = QueryState::READ_RESPONSE;
}

void AlthermaHub::read_response_() {
  auto uart = this->parent_;
#ifdef USE_MOCK_UART
  static MockUART mock_uart;
  uart = &mock_uart;
#endif

  if (millis() - this->query_started_at_ > QUERY_TIMEOUT_MS) {
    if (this->manual_query_active_) {
      this->publish_manual_query_statusf_(ESP_LOG_ERROR,
                                          "Manual query failed reg=0x%02X: timeout waiting for response",
                                          this->current_register_);
    } else {
      ESP_LOGE(TAG, "Timeout waiting for response for register 0x%02x", this->current_register_);
    }
    this->advance_register_();
    return;
  }

  const size_t available = uart->available();
  if (available == 0) {
    return;
  }

  const size_t remaining = this->expected_total_ - this->rx_len_;
  const size_t to_read = available < remaining ? available : remaining;
  uint8_t byte;

  if (to_read > 0 && uart->read_array(this->rx_buffer_ + this->rx_len_, to_read)) {
    this->rx_len_ += to_read;
  } else if (uart->read_byte(&byte)) {
    // Fallback if available() changed since sampling.
    this->rx_buffer_[this->rx_len_++] = byte;
  } else {
    return;
  }

  if (this->rx_len_ >= 3) {
    this->expected_total_ = static_cast<size_t>(this->rx_buffer_[2]) + 2;
    if (this->expected_total_ > RX_BUFFER_SIZE) {
      if (this->manual_query_active_) {
        this->publish_manual_query_statusf_(ESP_LOG_ERROR,
                                            "Manual query failed reg=0x%02X: invalid frame length 0x%02X",
                                            this->current_register_, this->rx_buffer_[2]);
      } else {
        ESP_LOGE(TAG, "Invalid frame length 0x%02x for register 0x%02x", this->rx_buffer_[2], this->current_register_);
      }
      this->advance_register_();
      return;
    }
  }

  if (this->rx_len_ >= this->expected_total_) {
    this->handle_complete_frame_();
  }
}

void AlthermaHub::handle_complete_frame_() {
  unsigned char crc = calculate_crc(this->rx_buffer_, this->rx_len_ - 1);
  if (crc != this->rx_buffer_[this->rx_len_ - 1]) {
    if (this->manual_query_active_) {
      this->publish_manual_query_statusf_(ESP_LOG_ERROR,
                                          "Manual query failed reg=0x%02X: CRC invalid 0x%02X (expected 0x%02X)",
                                          this->current_register_, this->rx_buffer_[this->rx_len_ - 1], crc);
    } else {
      ESP_LOGE(TAG, "CRC Invalid: 0x%02x (expected 0x%02x)", this->rx_buffer_[this->rx_len_ - 1], crc);
    }
    this->advance_register_();
    return;
  }

  if (this->rx_buffer_[1] != this->current_register_) {
    if (this->manual_query_active_) {
      this->publish_manual_query_statusf_(ESP_LOG_ERROR,
                                          "Manual query failed reg=0x%02X: response for 0x%02X",
                                          this->current_register_, this->rx_buffer_[1]);
    } else {
      ESP_LOGE(TAG, "Invalid register response: 0x%02x - asked for 0x%02x", this->rx_buffer_[1], this->current_register_);
    }
    this->advance_register_();
    return;
  }

  if (this->manual_query_active_) {
    LabelDef label(
      this->manual_register_,
      this->manual_offset_,
      this->manual_convid_,
      this->manual_datasize_,
      1,
      "manual_query"
    );

    unsigned char *input = this->rx_buffer_ + static_cast<size_t>(this->manual_offset_) + 3;
    this->converter_->convert(&label, input);

    char frame_hex[(RX_BUFFER_SIZE * 3) + 1] = {0};
    size_t write_pos = 0;
    for (size_t i = 0; i < this->rx_len_ && write_pos + 4 < sizeof(frame_hex); i++) {
      int written = snprintf(frame_hex + write_pos, sizeof(frame_hex) - write_pos,
                             (i + 1 < this->rx_len_) ? "%02X " : "%02X", this->rx_buffer_[i]);
      if (written <= 0) {
        break;
      }
      write_pos += static_cast<size_t>(written);
    }

    this->publish_manual_query_statusf_(ESP_LOG_INFO,
                                        "Manual query result reg=0x%02X value=%s buffer=%s",
                                        this->manual_register_, label.asString, frame_hex);

    this->manual_query_active_ = false;
  } else {
    LabelDef label;
    for (auto *sensor : this->sensors_) {
      if (sensor->registry_id() != this->current_register_) {
        continue;
      }

      if (this->decode_label(sensor, this->rx_buffer_, this->rx_len_, label)) {
        sensor->publish_state(label.asString);
      }
    }
  }

  auto delta = millis() - this->query_started_at_;
  ESP_LOGD(TAG, "Query register 0x%02x OK CRC: 0x%02x Length: 0x%02x Time: %d ms",
           this->rx_buffer_[1], this->rx_buffer_[this->rx_len_ - 1], this->rx_buffer_[2], delta);
  this->advance_register_();
}

void AlthermaHub::advance_register_() {
  if (this->manual_query_active_ || !this->registers_.size()) {
    this->manual_query_active_ = false;
    this->poll_active_ = false;
    this->query_state_ = QueryState::IDLE;
    return;
  }
    
  this->register_index_++;
  if (this->register_index_ >= this->registers_.size()) {
    this->poll_active_ = false;
    this->query_state_ = QueryState::IDLE;
    const auto update_delta = millis() - this->cycle_started_at_;
    ESP_LOGD(TAG, "Update end (%u ms)", update_delta);
    return;
  }

  this->query_state_ = QueryState::SEND_QUERY;
}

unsigned char AlthermaHub::calculate_crc(unsigned char *src, size_t len) {
  unsigned char b = 0;
  for (size_t i = 0; i < len; i++) {
    b += src[i];
  }
  return ~b;
}

}  // namespace altherma_hub
}  // namespace esphome