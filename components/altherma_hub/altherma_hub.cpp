#include "altherma_hub.h"
#include "esphome/core/log.h"
#include "labeldef.h"

#include "mock_uart.h"
// Work arounds to satisfy converters.h
LabelDef labelDefs[] = {};

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

#include "converters.h"

namespace esphome {
namespace altherma_hub {

static const char *TAG = "altherma_hub";
static constexpr size_t BUFFER_SIZE = 64;

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
}

AlthermaHub::~AlthermaHub() {
  delete this->converter_;
}

void AlthermaHub::update() {
  ESP_LOGD(TAG, "Update start");

  unsigned char buff[BUFFER_SIZE] = {0};
  LabelDef label;

  for (auto reg : this->registers_) {
    ESP_LOGD(TAG, "Register 0x%02X", reg);

    if (!this->query_registry(reg, buff)) {
      continue;
    }

    for (auto *sensor : sensors_) {
      if (sensor->registry_id() != reg) {
        continue;
      }
      if (decode_label(sensor, buff, label)) {
        sensor->publish_state(label.asString);
      }
    }
  }
  
  ESP_LOGD(TAG, "Update end");
}

bool AlthermaHub::decode_label(AlthermaSensorBase *sensor, unsigned char *frame, LabelDef &out) {
  out = LabelDef(
    sensor->registry_id(),
    sensor->offset(),
    sensor->convid(),
    sensor->datasize(),
    1,
    sensor->get_name().c_str()
  );

  unsigned char *input = frame + sensor->offset() + 3;
  this->converter_->convert(&out, input);

  return true;
}

bool AlthermaHub::query_registry(uint8_t regID, unsigned char *buffer) {
  auto uart = this->parent_;
#ifdef USE_MOCK_UART
  ESP_LOGW(TAG, "Using MockUART");
  uart = new MockUART();
#endif

  uint8_t command[] = {0x03, 0x40, regID, 0x00};
  command[3] = calculate_crc(command, 3);

  ESP_LOGV(TAG, "Querying register 0x%02x... CRC: 0x%02x", regID, command[3]);  
  uart->flush();
  uart->write_array(command, 4);

  const uint32_t start_time = millis();
  int len = 0;
  buffer[2] = 10;  // expected payload length
  uint8_t byte;

  while (len < buffer[2] + 2) {
     if (uart->available()) {
      if (uart->read_byte(&byte)) {
        buffer[len++] = byte;
      }
    } else if (millis() - start_time > 100) {
      ESP_LOGE(TAG, "Timeout waiting for response for register 0x%02x", regID);
      return false;
    }
    yield();
  }

  unsigned char crc = calculate_crc(buffer, len - 1);
  if (crc != buffer[len - 1]) {
    ESP_LOGE(TAG, "CRC Invalid: 0x%02x (expected 0x%02x)", buffer[len - 1], crc);
    return false;
  }

  if (buffer[1] != regID) {
    ESP_LOGE(TAG, "Invalid register response: 0x%02x - asked for 0x%02x", buffer[1], regID);
    return false;
  }

  auto delta = millis() - start_time;
  ESP_LOGD(TAG, "Query register 0x%02x OK CRC: 0x%02x Length: 0x%02x Time: %d ms", buffer[1], buffer[len - 1], buffer[2], delta);
  return true;
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