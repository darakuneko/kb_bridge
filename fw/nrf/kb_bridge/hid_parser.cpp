// KB Bridge nRF: HID report descriptor parser.
#include "hid_parser.h"

namespace {

struct Globals {
  uint16_t usage_page = 0;
  int32_t logical_min = 0, logical_max = 0;
  uint8_t report_size = 0;
  uint8_t report_count = 0;
  uint8_t report_id = 0;
};

struct Locals {
  uint8_t n_usages = 0;
  uint32_t usages[8];             // may carry a page in the upper 16 bits (4-byte usages)
  uint32_t usage_min = 0, usage_max = 0;
  bool has_min = false, has_max = false;
  void reset() { n_usages = 0; has_min = has_max = false; usage_min = usage_max = 0; }
};

}  // namespace

int32_t hid_sign_extend(uint32_t v, uint8_t bits) {
  if (bits == 0 || bits >= 32) return (int32_t)v;
  uint32_t m = 1UL << (bits - 1);
  return (int32_t)((v ^ m) - m);
}

uint32_t hid_get_bits(const uint8_t *data, size_t len, uint16_t off, uint8_t size) {
  uint32_t v = 0;
  for (uint8_t i = 0; i < size && i < 32; i++) {
    uint16_t bit = off + i;
    if ((size_t)(bit >> 3) >= len) break;
    if (data[bit >> 3] & (1 << (bit & 7))) v |= 1UL << i;
  }
  return v;
}

void hid_put_bits(uint8_t *data, size_t len, uint16_t off, uint8_t size, uint32_t value) {
  for (uint8_t i = 0; i < size && i < 32; i++) {
    uint16_t bit = off + i;
    if ((size_t)(bit >> 3) >= len) break;
    if (value & (1UL << i)) data[bit >> 3] |= (1 << (bit & 7));
    else data[bit >> 3] &= ~(1 << (bit & 7));
  }
}

uint16_t HidDescriptor::report_bytes(uint8_t type, uint8_t id) const {
  for (auto &s : sizes)
    if (s.type == type && s.id == id) return (s.bits + 7) / 8;
  return 0;
}

bool HidDescriptor::has_app_collection(uint16_t page, uint16_t usage) const {
  uint32_t key = ((uint32_t)page << 16) | usage;
  for (auto k : app_collections)
    if (k == key) return true;
  return false;
}

bool HidDescriptor::parse(const uint8_t *d, size_t len) {
  clear();
  Globals g;
  Globals stack[4];
  uint8_t sp = 0;
  Locals l;
  uint8_t depth = 0;
  uint16_t app_page = 0, app_usage = 0;

  auto bits_for = [this](uint8_t type, uint8_t id) -> uint16_t & {
    for (auto &s : sizes)
      if (s.type == type && s.id == id) return s.bits;
    sizes.push_back({type, id, 0});
    return sizes.back().bits;
  };

  size_t p = 0;
  while (p < len) {
    uint8_t prefix = d[p++];
    if (prefix == 0xFE) {                           // long item: skip
      if (p + 2 > len) return false;
      p += 2 + d[p];
      continue;
    }
    uint8_t size = prefix & 0x03;
    if (size == 3) size = 4;
    uint8_t type = (prefix >> 2) & 0x03;
    uint8_t tag = prefix >> 4;
    if (p + size > len) return false;
    uint32_t u = 0;
    for (uint8_t i = 0; i < size; i++) u |= (uint32_t)d[p + i] << (8 * i);
    int32_t s = size ? hid_sign_extend(u, size * 8) : 0;
    p += size;

    if (type == 0) {                                // ---- main
      if (tag == 0x8 || tag == 0x9 || tag == 0xB) { // Input / Output / Feature
        uint8_t rtype = tag == 0x8 ? HID_INPUT : tag == 0x9 ? HID_OUTPUT : HID_FEATURE;
        uint16_t &cursor = bits_for(rtype, g.report_id);
        uint16_t total = (uint16_t)g.report_size * g.report_count;
        bool constant = u & 0x01;
        if (!constant && g.report_size && g.report_count) {
          HidField f{};
          f.report_type = rtype;
          f.report_id = g.report_id;
          f.bit_offset = cursor;
          f.bit_size = g.report_size;
          f.count = g.report_count;
          f.variable = u & 0x02;
          f.usage_page = g.usage_page;
          f.logical_min = g.logical_min;
          f.logical_max = g.logical_max;
          f.app_page = app_page;
          f.app_usage = app_usage;
          if (l.n_usages && !(l.has_min && l.has_max)) {
            f.n_usages = l.n_usages;
            for (uint8_t i = 0; i < l.n_usages; i++) f.usages[i] = (uint16_t)l.usages[i];
            if (l.usages[0] >> 16) f.usage_page = l.usages[0] >> 16;
          } else {
            f.usage_min = (uint16_t)l.usage_min;
            f.usage_max = (uint16_t)(l.has_max ? l.usage_max : l.usage_min);
            if (l.usage_min >> 16) f.usage_page = l.usage_min >> 16;
          }
          fields.push_back(f);
        }
        cursor += total;
        l.reset();
      } else if (tag == 0xA) {                      // Collection
        if (depth == 0 && u == 0x01) {              // top-level application
          uint32_t usage = l.n_usages ? l.usages[0] : l.usage_min;
          app_page = (usage >> 16) ? (usage >> 16) : g.usage_page;
          app_usage = (uint16_t)usage;
          app_collections.push_back(((uint32_t)app_page << 16) | app_usage);
        }
        depth++;
        l.reset();
      } else if (tag == 0xC) {                      // End Collection
        if (depth) depth--;
        if (depth == 0) app_page = app_usage = 0;
        l.reset();
      }
    } else if (type == 1) {                         // ---- global
      switch (tag) {
        case 0x0: g.usage_page = (uint16_t)u; break;
        case 0x1: g.logical_min = s; break;
        case 0x2: g.logical_max = (g.logical_min >= 0 && size) ? (int32_t)u : s; break;
        case 0x7: g.report_size = (uint8_t)u; break;
        case 0x8: g.report_id = (uint8_t)u; uses_ids = true; break;
        case 0x9: g.report_count = (uint8_t)u; break;
        case 0xA: if (sp < 4) stack[sp++] = g; break;
        case 0xB: if (sp) g = stack[--sp]; break;
        default: break;                             // physical, unit, exponent: not needed
      }
    } else if (type == 2) {                         // ---- local
      switch (tag) {
        case 0x0: if (l.n_usages < 8) l.usages[l.n_usages++] = size == 4 ? u : (u & 0xFFFF); break;
        case 0x1: l.usage_min = size == 4 ? u : (u & 0xFFFF); l.has_min = true; break;
        case 0x2: l.usage_max = size == 4 ? u : (u & 0xFFFF); l.has_max = true; break;
        default: break;
      }
    }
  }
  return true;
}
