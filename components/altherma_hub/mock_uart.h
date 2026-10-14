#pragma once

#include "esphome/components/uart/uart_component.h"
#include "esphome/core/version.h"
#include <queue>
#include <vector>

namespace esphome
{
    namespace altherma_hub
    {
        static const char *TAG_MOCK = "mock_uart";

        class MockUART : public uart::UARTComponent
        {
        public:
            void write_array(const uint8_t *data, size_t len) override {
                if (len < 3) return;
                ESP_LOGI(TAG_MOCK, "Mock X10A cmd=0x%02X bytes=%u", data[1], (unsigned) len);
                if (data[1] == 0x40 && len >= 4) {
                    generate_response(data[2]);
                } else {
                    generate_diagnostic_response(data[1]);
                }
            }

            size_t available() override {
                return rx_buffer_.size();
            }

            esphome::uart::UARTFlushResult flush() override {
                while (!rx_buffer_.empty()) {
                    rx_buffer_.pop();
                }
                return esphome::uart::UARTFlushResult::UART_FLUSH_RESULT_SUCCESS;
            }
            
            bool read_byte(uint8_t *data) {
                if (rx_buffer_.empty())
                    return false;
                
                *data = rx_buffer_.front();
                rx_buffer_.pop();
                return true;
            }

            bool peek_byte(uint8_t *data) override {
                if (rx_buffer_.empty())
                    return false;
                *data = rx_buffer_.front();
                return true;
            }

            bool read_array(uint8_t *data, size_t len) override {
                if (rx_buffer_.size() < len)
                return false;
                for (size_t i = 0; i < len; i++) {
                data[i] = rx_buffer_.front();
                rx_buffer_.pop();
                }
                return true;
            }

            void check_logger_conflict() override {}

        protected:
            void generate_diagnostic_response(uint8_t cmd) {
                std::vector<uint8_t> response;
                switch (cmd) {
                  case 0x4C:
                    response = {0x4C, 0x04, 0x29, 0x00, 0x18};
                    break;
                  case 0x52:
                    response.push_back(0x52);
                    for (size_t idx = 0; idx < 16; idx++) response.push_back(0x00);
                    break;
                  case 0x49:
                    response = {0x49, 0x00};
                    break;
                  default:
                    response = {0x15, 0xEA}; // 0x21 field read is not simulated
                    break;
                }
                if (response[0] != 0x15) {
                    uint8_t sum = 0;
                    for (auto byte : response) sum = static_cast<uint8_t>(sum + byte);
                    response.push_back(static_cast<uint8_t>(~sum));
                }
                for (auto byte : response) rx_buffer_.push(byte);
            }

            void generate_response(uint8_t reg_id)
            {
                // Simulate processing delay
                delay(10);

                std::vector<uint8_t> response;

                // Generate mock data based on register
                switch (reg_id)
                {
                case 0x60:
                    response = {
                        0x40, 0x60, 0x13, 0x80, 0x00, 0x18, 0x00, 0x00,
                        0x00, 0x00, 0xC2, 0x01, 0xC1, 0x01, 0xE0, 0x02,
                        0x23, 0x91, 0x82, 0x00, 0x17
                    };
                    break;
                    

                case 0x21:
                    response = {
                        0x40, 0x21, 0x12, 0xF9, 0x00, 0x95, 0x00, 0xE6, 
                        0x00, 0xA8, 0xCE, 0xFF, 0x67, 0x01, 0x1A, 0x00, 
                        0xC4, 0xFF, 0x00, 0x5E
                    };
                    break;

                default:
                    // Generic response for unknown registers
                    response = {
                        0x40, reg_id, 0x08,
                        0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00};
                    break;
                }

                // Add response to rx buffer
                for (uint8_t byte : response){
                    rx_buffer_.push(byte);
                }

                ESP_LOGI(TAG_MOCK, "Generated response for reg 0x%02x, len=%d", reg_id, response[2]);
            }

            std::queue<uint8_t> rx_buffer_;
        };

    } // namespace altherma_hub
} // namespace esphome
