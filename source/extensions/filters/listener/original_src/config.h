#pragma once

#include "envoy/extensions/filters/listener/original_src/v3alpha/original_src.pb.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalSrc {
class Config {
public:
  Config() = default;
  Config(const envoy::extensions::filters::listener::original_src::v3alpha::OriginalSrc& config);

  bool usePort() const { return use_port_; }
  uint32_t mark() const { return mark_; }

private:
  bool use_port_ = false;
  uint32_t mark_ = 0;
};
} // namespace OriginalSrc
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
