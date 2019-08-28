#pragma once

#include "envoy/buffer/buffer.h"

#include "common/common/non_copyable.h"

namespace Envoy {
namespace Buffer {

/**
 * An implementation of BufferFragment backed by envoy_data.
 */
class BridgeFragment : NonCopyable, public BufferFragment {
public:
  BridgeFragment(envoy_data data) : data_(data) {}

  // Buffer::BufferFragment
  const void* data() const override { return data_.bytes; }
  size_t size() const override { return data_.length; }
  void done() override {
    data_.release(data_.context);
    delete this;
  }

private:
  envoy_data data_;
};

} // namespace Buffer
} // namespace Envoy
