#include "../components/altherma_hub/x10a_read_protocol.h"
#include <cassert>
#include <cstring>

using namespace esphome::altherma_hub::x10a;

int main() {
  uint8_t frame[16] = {};
  assert(build_frame(0x4C, nullptr, 0, frame, sizeof(frame)) == 3);
  assert(frame[0] == 0x02 && frame[1] == 0x4C && frame[2] == 0xB1);
  const uint8_t addr[] = {0x7A, 0x18};
  assert(build_frame(0x52, addr, sizeof(addr), frame, sizeof(frame)) == 5);
  const uint8_t ram_query[] = {0x04, 0x52, 0x7A, 0x18, 0x17};
  assert(std::memcmp(frame, ram_query, 5) == 0);
  const uint8_t memory_addr[] = {0x6E, 0x01};
  assert(build_frame(0x49, memory_addr, sizeof(memory_addr), frame, sizeof(frame)) == 5);
  const uint8_t mem_query[] = {0x04, 0x49, 0x6E, 0x01, 0x43};
  assert(std::memcmp(frame, mem_query, 5) == 0);
  const uint8_t setting[] = {0x49, 0x00, 0x01, 0x01, 0x05, 0x05};
  assert(build_frame(0x21, setting, sizeof(setting), frame, sizeof(frame)) == 9);
  const uint8_t setting_query[] = {0x08, 0x21, 0x49, 0x00, 0x01, 0x01, 0x05, 0x05, 0x81};
  assert(std::memcmp(frame, setting_query, 9) == 0);
  assert(build_frame(0x21, setting, sizeof(setting), frame, 8) == 0);
  uint8_t reply[] = {0x49, 0x42, 0x00};
  reply[2] = checksum(reply, 2);
  assert(valid_reply(reply, sizeof(reply), 0x49));
  reply[1]++;
  assert(!valid_reply(reply, sizeof(reply), 0x49));
  assert(!valid_reply(reply, sizeof(reply), 0x52));
  const uint8_t negative[] = {0x15, 0xEA};
  assert(nack(negative, sizeof(negative)));
  assert(!valid_reply(negative, sizeof(negative), 0x49));
  uint8_t page = 0, field = 0;
  assert(parse_field_code("[0-0B]", page, field) && page == 0 && field == 11);
  assert(parse_field_code("1-02", page, field) && page == 1 && field == 2);
  assert(parse_field_code("f-0e", page, field) && page == 15 && field == 14);
  assert(!parse_field_code("1-0F", page, field));
  assert(!parse_field_code("0x1-02", page, field));
  assert(!parse_field_code("[1-0Z]", page, field));
  assert(valid_read_range(0x187A, 64, true));
  assert(valid_read_range(0x016E, 64, false));
  assert(!valid_read_range(0xFFF8, 1, true));
  assert(valid_read_range(0xFFFF, 1, false));
  assert(!valid_read_range(0xFFFF, 2, false));
  assert(!valid_read_range(0x1000, 65, false));
  return 0;
}
