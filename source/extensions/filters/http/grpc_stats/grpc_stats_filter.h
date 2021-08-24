#pragma once

#include "envoy/extensions/filters/http/grpc_stats/v3/config.pb.h"
#include "envoy/extensions/filters/http/grpc_stats/v3/config.pb.validate.h"
#include "envoy/server/filter_config.h"
#include "envoy/stream_info/filter_state.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcStats {

// Filter state exposing the gRPC message counts.
struct GrpcStatsObject : public StreamInfo::FilterState::Object {
  uint64_t request_message_count = 0;
  uint64_t response_message_count = 0;

  ProtobufTypes::MessagePtr serializeAsProto() const override {
    auto msg = std::make_unique<envoy::extensions::filters::http::grpc_stats::v3::FilterObject>();
    msg->set_request_message_count(request_message_count);
    msg->set_response_message_count(response_message_count);
    return msg;
  }

  absl::optional<std::string> serializeAsString() const override {
    return absl::StrCat(request_message_count, ",", response_message_count);
  }
};

class GrpcStatsFilterConfigFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::grpc_stats::v3::FilterConfig> {
public:
  GrpcStatsFilterConfigFactory() : FactoryBase("envoy.filters.http.grpc_stats") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::grpc_stats::v3::FilterConfig& proto_config,
      const std::string&, Server::Configuration::FactoryContext&) override;
};

} // namespace GrpcStats
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
