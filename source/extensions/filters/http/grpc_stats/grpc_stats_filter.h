#pragma once

#include "envoy/server/filter_config.h"
#include "envoy/stream_info/filter_state.h"

#include "extensions/filters/http/common/empty_http_filter_config.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcStats {

class GrpcStatsFilterConfig : public Common::EmptyHttpFilterConfig {
public:
  GrpcStatsFilterConfig() : Common::EmptyHttpFilterConfig(HttpFilterNames::get().GrpcStats) {}

  Http::FilterFactoryCb createFilter(const std::string&,
                                     Server::Configuration::FactoryContext&) override;
};

} // namespace GrpcStats
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
