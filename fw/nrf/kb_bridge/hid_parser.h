// KB Bridge nRF: HID report descriptor parser (just enough for keyboards, consumer/system
// control, mice and vendor raw HID) and bit-field extraction.
#pragma once
#include <Arduino.h>
#include <vector>

enum : uint8_t { HID_INPUT = 1, HID_OUTPUT = 2, HID_FEATURE = 3 };

struct HidField {
  uint8_t  report_type;           // HID_INPUT / HID_OUTPUT / HID_FEATURE
  uint8_t  report_id;             // 0 when the descriptor uses no report ids
  uint16_t bit_offset;            // from the start of the report data (after the id byte)
  uint8_t  bit_size;
  uint8_t  count;
  bool     variable;              // false = array (values are usage indexes)
  uint16_t usage_page;
  uint16_t usage_min, usage_max;  // range (arrays, bitmaps) when n_usages == 0
  uint8_t  n_usages;              // explicit usage list (variable fields like X/Y/Wheel)
  uint16_t usages[8];
  int32_t  logical_min, logical_max;
  uint16_t app_page, app_usage;   // top-level application collection

  uint16_t usage_at(uint8_t i) const {
    if (n_usages) return usages[i < n_usages ? i : n_usages - 1];
    uint32_t u = (uint32_t)usage_min + i;
    return u > usage_max ? usage_max : (uint16_t)u;
  }
};

struct HidReportSize {
  uint8_t type, id;
  uint16_t bits;
};

class HidDescriptor {
 public:
  bool parse(const uint8_t *desc, size_t len);
  void clear() { fields.clear(); sizes.clear(); uses_ids = false; app_collections.clear(); }
  uint16_t report_bytes(uint8_t type, uint8_t id) const;
  bool has_app_collection(uint16_t page, uint16_t usage) const;

  std::vector<HidField> fields;
  std::vector<HidReportSize> sizes;
  std::vector<uint32_t> app_collections;   // (page << 16) | usage
  bool uses_ids = false;
};

uint32_t hid_get_bits(const uint8_t *data, size_t len, uint16_t bit_offset, uint8_t bit_size);
void hid_put_bits(uint8_t *data, size_t len, uint16_t bit_offset, uint8_t bit_size, uint32_t value);
int32_t hid_sign_extend(uint32_t v, uint8_t bits);
