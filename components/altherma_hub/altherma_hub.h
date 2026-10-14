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
      void queue_read_memory(std::string space, std::string address, int32_t length);
      void queue_software_id();
      void queue_micon_id();
      void queue_identify_controller();
      void queue_field_setting(std::string code);
      void set_enable_memory_diagnostics(bool enabled) { this->enable_memory_diagnostics_ = enabled; }
      void set_enable_experimental_field_settings(bool enabled) { this->enable_experimental_field_settings_ = enabled; }
      void set_identify_on_boot(bool enabled) { this->identify_on_boot_ = enabled; }
      void set_software_id_text_sensor(text_sensor::TextSensor *sensor) { this->software_id_text_sensor_ = sensor; }
      void set_micon_id_text_sensor(text_sensor::TextSensor *sensor) { this->micon_id_text_sensor_ = sensor; }

    protected:
      enum class ManualQueryStatusLevel : uint8_t {
        INFO,
        WARN,
        ERROR,
      };

      enum class DiagnosticKind : uint8_t {
        NONE,
        SOFTWARE_ID,
        MICON_ID,
        RAM,
        MEMORY,
        FIELD_SETTING,
      };

      unsigned char calculate_crc(unsigned char *src, size_t len);
      bool decode_label(AlthermaSensorBase *sensor, unsigned char *frame, size_t frame_len, LabelDef &out);
      void publish_manual_query_statusf_(int level, const char *fmt, ...);
      void begin_poll_cycle_();
      void start_query_(uint8_t reg);
      void read_response_();
      void handle_complete_frame_();
      void advance_register_();
      bool reserve_diagnostic_(DiagnosticKind kind);
      void start_diagnostic_query_();
      void read_diagnostic_response_();
      void report_field_setting_response_(bool partial);
      void finish_diagnostic_();

      enum class QueryState : uint8_t {
        IDLE,
        SEND_QUERY,
        READ_RESPONSE,
      };

      std::vector<AlthermaSensorBase *> sensors_;
      std::vector<uint8_t> registers_;
      Converter *converter_{nullptr};

      static constexpr size_t RX_BUFFER_SIZE = 64;
      static constexpr uint32_t QUERY_TIMEOUT_MS = 300;
      static constexpr uint32_t DIAGNOSTIC_READ_TIMEOUT_MS = 500;
      static constexpr uint32_t DIAGNOSTIC_FIELD_TIMEOUT_MS = 700;
      static constexpr uint32_t DIAGNOSTIC_FIELD_GAP_MS = 100;

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

      bool enable_memory_diagnostics_{false};
      bool enable_experimental_field_settings_{false};
      bool identify_on_boot_{false};
      bool identification_sequence_active_{false};
      text_sensor::TextSensor *software_id_text_sensor_{nullptr};
      text_sensor::TextSensor *micon_id_text_sensor_{nullptr};
      DiagnosticKind diagnostic_kind_{DiagnosticKind::NONE};
      bool diagnostic_pending_{false};
      bool diagnostic_active_{false};
      uint8_t diagnostic_command_{0};
      uint8_t diagnostic_page_{0};
      uint8_t diagnostic_setting_{0};
      uint16_t diagnostic_address_{0};
      uint16_t diagnostic_length_{0};
      size_t diagnostic_received_{0};
      size_t diagnostic_expected_total_{0};
      uint32_t diagnostic_last_byte_at_{0};
      uint8_t diagnostic_data_[64] = {};
    };

  } // namespace altherma_hub
} // namespace esphome
