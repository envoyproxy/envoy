#pragma once

#include <chrono>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/network/dns.h"

namespace Envoy {
namespace Network {

class DnsPacketParser {
public:
  DnsPacketParser(std::vector<uint8_t> data) : data_(std::move(data)), offset_(0) {}

  bool remain(size_t len) const { return offset_ + len <= data_.size(); }

  bool read32(uint32_t& val) {
    if (!remain(4))
      return false;
    val = (data_[offset_] << 24) | (data_[offset_ + 1] << 16) | (data_[offset_ + 2] << 8) |
          data_[offset_ + 3];
    offset_ += 4;
    return true;
  }

  bool read16(uint16_t& val) {
    if (!remain(2))
      return false;
    val = (data_[offset_] << 8) | data_[offset_ + 1];
    offset_ += 2;
    return true;
  }

  bool read8(uint8_t& val) {
    if (!remain(1))
      return false;
    val = data_[offset_];
    offset_ += 1;
    return true;
  }

  bool skip(size_t len) {
    if (!remain(len))
      return false;
    offset_ += len;
    return true;
  }

  bool readBytes(std::vector<uint8_t>& out, size_t len) {
    if (!remain(len))
      return false;
    out.assign(data_.begin() + offset_, data_.begin() + offset_ + len);
    offset_ += len;
    return true;
  }

  bool skipDnsName() {
    while (true) {
      uint8_t len;
      if (!read8(len))
        return false;
      if (len == 0) {
        break;
      }
      if ((len & 0xC0) != 0) {
        if ((len & 0xC0) == 0xC0) {
          // Pointer (2 bytes total)
          uint8_t dummy;
          if (!read8(dummy))
            return false;
          break;
        }
        return false; // Reserved
      }
      if (!skip(len))
        return false;
    }
    return true;
  }

  std::list<DnsResponse> parseHttpsRecords() {
    std::list<DnsResponse> results;

    // 1. Parse Header
    uint16_t id, flags, qdcount, ancount, nscount, arcount;
    if (!read16(id) || !read16(flags) || !read16(qdcount) || !read16(ancount) || !read16(nscount) ||
        !read16(arcount)) {
      return {};
    }

    // Check if it's a response (QR bit = 1 in flags)
    if ((flags & 0x8000) == 0) {
      return {};
    }
    // Check RCODE. 0 = No error.
    if ((flags & 0x000F) != 0) {
      return {};
    }

    // 2. Skip Questions
    for (uint16_t i = 0; i < qdcount; ++i) {
      if (!skipDnsName())
        return {};
      uint16_t qtype, qclass;
      if (!read16(qtype) || !read16(qclass))
        return {};
    }

    // 3. Parse Answers
    for (uint16_t i = 0; i < ancount; ++i) {
      if (!skipDnsName())
        return {}; // Answer name
      uint16_t type, cls;
      uint32_t ttl;
      uint16_t rdlength;
      if (!read16(type) || !read16(cls) || !read32(ttl) || !read16(rdlength)) {
        return {};
      }

      if (type == 65 && cls == 1) { // HTTPS (65) and IN (1)
        std::vector<uint8_t> rdata;
        if (!readBytes(rdata, rdlength)) {
          return {};
        }
        results.emplace_back(DnsResponse(std::move(rdata), std::chrono::seconds(ttl)));
      } else {
        if (!skip(rdlength)) {
          return {};
        }
      }
    }

    return results;
  }

private:
  const std::vector<uint8_t> data_;
  size_t offset_;
};

} // namespace Network
} // namespace Envoy
