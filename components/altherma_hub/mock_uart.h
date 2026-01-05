#pragma once

#include "esphome/components/uart/uart_component.h"
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
                ESP_LOGI(TAG_MOCK, "Write: [%02x %02x %02x %02x]", data[0], data[1], data[2], data[3]);
                generate_response(data[2]);
            }

            int available() override {
                return rx_buffer_.size();
            }

            void flush() override {
                while (!rx_buffer_.empty()) {
                    rx_buffer_.pop();
                }
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