#pragma once

#include "envoy/stream_info/filter_state.h"

namespace Envoy {
namespace StreamInfo {

class TestIntAccessor : public FilterState::Object {
public:
  TestIntAccessor(int value) : value_(value) {}

  int access() const { return value_; }

private:
  int value_;
};

} // namespace StreamInfo
} // namespace Envoy
