#pragma once

#include "envoy/buffer/buffer.h"

#include "source/common/common/non_copyable.h"

#include "library/common/types/c_types.h"

namespace Envoy {
namespace Buffer {

/**
 * An implementation of BufferFragment backed by envoy_data.
 */
class BridgeFragment : NonCopyable, public BufferFragment {
public:
  // TODO: Consider moving this to a BridgeFragmentFactory class.
  static BridgeFragment* createBridgeFragment(envoy_data data) { return new BridgeFragment(data); }

  // Buffer::BufferFragment
  const void* data() const override { return data_.bytes; }
  size_t size() const override { return data_.length; }
  void done() override {
    data_.release(data_.context);
    delete this;
  }

private:
  BridgeFragment(envoy_data data) : data_(data) {}
  ~BridgeFragment() {}
  envoy_data data_;
};

} // namespace Buffer
} // namespace Envoy
