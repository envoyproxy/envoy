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
  Config(const envoy::extensions::filters::listener::original_dst::v3::OriginalDst& config,
         const envoy::config::core::v3::TrafficDirection& traffic_direction)
      : method_(getOriginalDstMethod(config)), traffic_direction_(traffic_direction) {}

  enum class OriginalDstMethod {
    SocketOption = 0,
    NoOp = 1,
  };
  OriginalDstMethod method_{OriginalDstMethod::SocketOption};
  envoy::config::core::v3::TrafficDirection traffic_direction_;

private:
  static OriginalDstMethod getOriginalDstMethod(
      const envoy::extensions::filters::listener::original_dst::v3::OriginalDst& config) {
    switch (config.address_recovery_method()) {
    case envoy::extensions::filters::listener::original_dst::v3::OriginalDst::Method::
        OriginalDst_Method_DNAT:
      return OriginalDstMethod::SocketOption;
    case envoy::extensions::filters::listener::original_dst::v3::OriginalDst::Method::
        OriginalDst_Method_CurrentLocalAddress:
      return OriginalDstMethod::NoOp;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }
};

} // namespace OriginalDst
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
