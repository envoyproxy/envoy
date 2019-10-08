#pragma once

#include "envoy/server/filter_config.h"
#include "envoy/stream_info/filter_state.h"

#include "extensions/filters/http/common/empty_http_filter_config.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcStreaming {

// Filter state exposing the gRPC message counts.
struct GrpcMessageCounterObject : public StreamInfo::FilterState::Object {
  uint64_t request_message_count = 0;
  uint64_t response_message_count = 0;
};

class GrpcStreamingFilterConfig : public Common::EmptyHttpFilterConfig {
public:
  GrpcStreamingFilterConfig()
      : Common::EmptyHttpFilterConfig(HttpFilterNames::get().GrpcStreaming) {}

  Http::FilterFactoryCb createFilter(const std::string&,
                                     Server::Configuration::FactoryContext&) override;
};

} // namespace GrpcStreaming
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
