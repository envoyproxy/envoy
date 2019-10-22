#pragma once

#include "envoy/config/filter/http/grpc_stats/v2alpha/config.pb.h"
#include "envoy/config/filter/http/grpc_stats/v2alpha/config.pb.validate.h"
#include "envoy/server/filter_config.h"
#include "envoy/stream_info/filter_state.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcStats {

// Filter state exposing the gRPC message counts.
struct GrpcStatsObject : public StreamInfo::FilterState::Object {
  uint64_t request_message_count = 0;
  uint64_t response_message_count = 0;
};

class GrpcStatsFilterConfig
    : public Common::FactoryBase<envoy::config::filter::http::grpc_stats::v2alpha::FilterConfig> {
public:
  GrpcStatsFilterConfig() : FactoryBase(HttpFilterNames::get().GrpcStats) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::http::grpc_stats::v2alpha::FilterConfig& config,
      const std::string&, Server::Configuration::FactoryContext&) override;
};

} // namespace GrpcStats
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
