#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/api/custom_api_device.h"
#include "esphome/components/uart/uart.h"
#include <vector>

class LabelDef;

namespace esphome
{
  namespace altherma_hub
  {
    class Converter;

    class AlthermaSensorBase
    {
    public:
      virtual ~AlthermaSensorBase() = default;

      // Pure virtual functions that derived classes must implement
      virtual std::string get_name() const = 0;
      virtual void publish_state(const std::string &state) = 0;

      void set_registry_id(uint8_t v) { registry_id_ = v; }
      void set_offset(int v) { offset_ = v; }
      void set_convid(int v) { convid_ = v; }
      void set_datasize(int v) { datasize_ = v; }

      uint8_t registry_id() const { return registry_id_; }
      int offset() const { return offset_; }
      int convid() const { return convid_; }
      int datasize() const { return datasize_; }

    protected:
      int registry_id_;
      int offset_;
      int convid_;
      int datasize_;
    };

    class AlthermaSensor
        : public sensor::Sensor,
          public AlthermaSensorBase
    {
    public:
      std::string get_name() const override {
        return sensor::Sensor::get_name();
      }
      
      void publish_state(const std::string &state) override {
        char *end = nullptr;
        double value = strtod(state.c_str(), &end);
        
        // Valid number only if conversion consumed something and no junk afterwards
        if (end != state.c_str() && *end == '\0') {
          sensor::Sensor::publish_state(value);
        } else {
          ESP_LOGW("AlthermaSensor", "Non-numeric value '%s' for %s",
                  state.c_str(), get_name().c_str());
        }
      }
    };

    class AlthermaTextSensor
        : public text_sensor::TextSensor,
          public AlthermaSensorBase
    {
    public:
      std::string get_name() const override {
        return text_sensor::TextSensor::get_name();
      }

      void publish_state(const std::string &state) override {
        text_sensor::TextSensor::publish_state(state);
      }
    };

    class AlthermaBinarySensor
        : public binary_sensor::BinarySensor,
          public AlthermaSensorBase
    {
    public:
      std::string get_name() const override {
        return binary_sensor::BinarySensor::get_name();
      }

      void publish_state(const std::string &state) override {
        // Convert string to boolean
        std::string lower = state;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        
        bool value = (lower == "true" || lower == "1" || 
                      lower == "on" || lower == "yes");
        
        binary_sensor::BinarySensor::publish_state(value);
      }
    };

    class AlthermaHub : public PollingComponent, public uart::UARTDevice, public api::CustomAPIDevice
    {
    public:
      ~AlthermaHub();
      
      void register_sensor(AlthermaSensorBase *sensor);
      void set_query_result_text_sensor(text_sensor::TextSensor *text_sensor) {
        this->query_result_text_sensor_ = text_sensor;
      }

      void setup() override;
      void update() override;
      void loop() override;
      void queue_manual_query(std::string registry_id, int32_t offset, int32_t convid, int32_t datasize);

    protected:
      enum class ManualQueryStatusLevel : uint8_t {
        INFO,
        WARN,
        ERROR,
      };

      unsigned char calculate_crc(unsigned char *src, size_t len);
      bool decode_label(AlthermaSensorBase *sensor, unsigned char *frame, size_t frame_len, LabelDef &out);
      void publish_manual_query_statusf_(esp_log_level_t level, const char *fmt, ...);
      void begin_poll_cycle_();
      void start_query_(uint8_t reg);
      void read_response_();
      void handle_complete_frame_();
      void advance_register_();

      enum class QueryState : uint8_t {
        IDLE,
        SEND_QUERY,
        READ_RESPONSE,
      };

      std::vector<AlthermaSensorBase *> sensors_;
      std::vector<uint8_t> registers_;
      Converter *converter_{nullptr};

      static constexpr size_t RX_BUFFER_SIZE = 64;
      static constexpr uint32_t QUERY_TIMEOUT_MS = 100;

      QueryState query_state_{QueryState::IDLE};
      bool poll_active_{false};
      size_t register_index_{0};
      uint8_t current_register_{0};
      size_t rx_len_{0};
      size_t expected_total_{0};
      uint32_t query_started_at_{0};
      uint32_t cycle_started_at_{0};
      unsigned char rx_buffer_[RX_BUFFER_SIZE] = {0};

      bool manual_query_pending_{false};
      bool manual_query_active_{false};

      uint8_t manual_register_{0};
      int32_t manual_offset_{0};
      int32_t manual_convid_{0};
      int32_t manual_datasize_{0};

      text_sensor::TextSensor *query_result_text_sensor_{nullptr};
    };

  } // namespace altherma_hub
} // namespace esphome