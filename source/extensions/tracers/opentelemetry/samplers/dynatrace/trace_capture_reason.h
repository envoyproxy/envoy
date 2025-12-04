#pragma once

#include <string>
#include <vector>

#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

class TraceCaptureReason {

  using Version = uint8_t;
  using PayloadBitMask = uint64_t;

  static const size_t MAX_PAYLOAD_SIZE = sizeof(Version) + sizeof(PayloadBitMask);
  static constexpr absl::string_view ATM = "atm";
  static constexpr absl::string_view FIXED = "fixed";
  static constexpr absl::string_view CUSTOM = "custom";
  static constexpr absl::string_view MAINFRAME = "mainframe";
  static constexpr absl::string_view SERVERLESS = "serverless";
  static constexpr absl::string_view RUM = "rum";
  static constexpr absl::string_view UNKNOWN = "unknown";

public:
  enum { InvalidVersion = 0u, Version1 = 1u };

  // Bit mask values
  enum Reason : PayloadBitMask {
    Undefined = 0ull,
    Atm = 1ull << 0,
    Fixed = 1ull << 1,
    Custom = 1ull << 2,
    Mainframe = 1ull << 3,
    Serverless = 1ull << 4,
    Rum = 1ull << 5,
  };

  static TraceCaptureReason createInvalid() { return {false, 0}; }

  static TraceCaptureReason create(PayloadBitMask tcr_bitmask) { return {true, tcr_bitmask}; }

  static TraceCaptureReason create(const absl::string_view& payload_hex) {
    if (payload_hex.size() < 4 || payload_hex.size() % 2 != 0) {
      // At least 1 byte for version and 1 byte for bitmask (2 hex chars each)
      return createInvalid();
    }

    // validate the size of the payload
    size_t payload_bytes = payload_hex.size() / 2;
    if (payload_bytes <= sizeof(Version) || payload_bytes > MAX_PAYLOAD_SIZE) {
      return createInvalid();
    }

    // Parse version (first byte, 2 hex chars)
    uint32_t version = 0;
    if (!absl::SimpleHexAtoi(payload_hex.substr(0, 2), &version) ||
        version != TraceCaptureReason::Version1) {
      return createInvalid();
    }

    uint64_t bitmask = 0;
    if (!absl::SimpleHexAtoi(payload_hex.substr(2), &bitmask)) {
      return createInvalid();
    }

    constexpr PayloadBitMask kValidMask = Atm | Fixed | Custom | Mainframe | Serverless | Rum;
    if ((bitmask & ~kValidMask) != 0) {
      return createInvalid();
    }
    return create(bitmask);
  }

  // Returns true if parsing was successful.
  bool isValid() const { return valid_; };

  std::vector<absl::string_view> toSpanAttributeValue() const {
    std::vector<absl::string_view> result;
    if (tcr_bitmask_ & Atm) {
      result.push_back(ATM);
    }
    if (tcr_bitmask_ & Fixed) {
      result.push_back(FIXED);
    }
    if (tcr_bitmask_ & Custom) {
      result.push_back(CUSTOM);
    }
    if (tcr_bitmask_ & Mainframe) {
      result.push_back(MAINFRAME);
    }
    if (tcr_bitmask_ & Serverless) {
      result.push_back(SERVERLESS);
    }
    if (tcr_bitmask_ & Rum) {
      result.push_back(RUM);
    }
    if (result.empty()) {
      result.push_back(UNKNOWN);
    }
    return result;
  }

  std::string bitmaskHex() const {
    return absl::StrCat(absl::Hex(static_cast<uint8_t>(tcr_bitmask_), absl::kZeroPad2));
  }

private:
  TraceCaptureReason(bool valid, PayloadBitMask tcr_bitmask)
      : valid_(valid), tcr_bitmask_(tcr_bitmask) {}

  const bool valid_;
  const PayloadBitMask tcr_bitmask_;
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
