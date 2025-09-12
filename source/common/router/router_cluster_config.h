#pragma once

#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Router {

/**
 * Protocol options config for router cluster options.
 */
class RouterClusterProtocolOptionsConfig : public Upstream::ProtocolOptionsConfig {
public:
  explicit RouterClusterProtocolOptionsConfig(bool enable_copy_on_write)
      : enable_copy_on_write_(enable_copy_on_write) {}

  bool enableCopyOnWrite() const { return enable_copy_on_write_; }

private:
  const bool enable_copy_on_write_;
};

} // namespace Router
} // namespace Envoy
