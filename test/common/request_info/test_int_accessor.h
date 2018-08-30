#pragma once

#include "envoy/request_info/filter_state.h"

namespace Envoy {
namespace RequestInfo {

class TestIntAccessor : public FilterState::Object {
public:
  TestIntAccessor(int value) : value_(value) {}

  int access() const { return value_; }

private:
  int value_;
};

} // namespace RequestInfo
} // namespace Envoy
