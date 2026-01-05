#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/uart/uart.h"
#include <vector>

class LabelDef;
class Converter;

namespace esphome
{
  namespace altherma_hub
  {

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

    class AlthermaHub : public PollingComponent, public uart::UARTDevice
    {
    public:
      ~AlthermaHub();
      
      void register_sensor(AlthermaSensorBase *sensor);

      void setup() override;
      void update() override;

    protected:
      bool query_registry(uint8_t reg, unsigned char *buffer);
      unsigned char calculate_crc(unsigned char *src, size_t len);
      bool decode_label(AlthermaSensorBase *sensor, unsigned char *frame, LabelDef &out);

      std::vector<AlthermaSensorBase *> sensors_;
      std::vector<uint8_t> registers_;
      Converter *converter_{nullptr}; 
    };

  } // namespace altherma_hub
} // namespace esphome