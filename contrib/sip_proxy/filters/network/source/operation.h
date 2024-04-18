#pragma once

#include <string>

#include "absl/types/variant.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

enum class OperationType {
  Invalid = 0,
  Insert = 1,
  Delete = 2,
  Modify = 3,
  Query = 4, // not used yet.
};

struct InsertOperationValue {
  InsertOperationValue(std::string&& value) : value_(value) {}
  std::string value_;
};
struct DeleteOperationValue {
  DeleteOperationValue(size_t length) : length_(length) {}
  size_t length_;
};
struct ModifyOperationValue {
  ModifyOperationValue(size_t src_length, std::string&& dest)
      : src_length_(src_length), dest_(dest) {}
  size_t src_length_;
  std::string dest_;
};

class Operation {
public:
  Operation(OperationType type, size_t position,
            absl::variant<InsertOperationValue, DeleteOperationValue, ModifyOperationValue> value)
      : type_(type), position_(position), value_(value) {}

  // constexpr bool operator<(const Operation& other) { return this->position_ < other.position_; }
  // constexpr bool operator>(const Operation& other) { return this->position_ > other.position_; }
  // constexpr bool operator==(const Operation& other) { return this->position_ == other.position_;
  // } constexpr bool operator!=(const Operation& other) { return this->position_ !=
  // other.position_; } constexpr bool operator<=(const Operation& other) { return this->position_
  // <= other.position_; } constexpr bool operator>=(const Operation& other) { return
  // this->position_ >= other.position_; } constexpr bool operator<=>(Operation &other) { return
  // this->position_ <=> other.position_; }

  // private:
  OperationType type_;
  size_t position_;
  absl::variant<InsertOperationValue, DeleteOperationValue, ModifyOperationValue> value_;
};

static constexpr bool operator<(const Operation& o1, const Operation& o2) {
  return o1.position_ < o2.position_;
}

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
