#pragma once

#include "envoy/extensions/filters/listener/original_dst/v3/original_dst.pb.h"
#include "envoy/extensions/filters/listener/original_dst/v3/original_dst.pb.validate.h"
#include "envoy/network/filter.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalDst {

class Config {
public:
  Config() = default;
  Config(const envoy::extensions::filters::listener::original_dst::v3::OriginalDst&, // TODO,
         const envoy::config::core::v3::TrafficDirection& traffic_direction)
      : method_(0), traffic_direction_(traffic_direction) {}

  uint32_t method_{0};
  envoy::config::core::v3::TrafficDirection traffic_direction_;
};

} // namespace OriginalDst
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy