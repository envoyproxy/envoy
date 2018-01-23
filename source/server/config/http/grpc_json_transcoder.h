#pragma once

#include <string>

#include "envoy/api/v2/filter/http/transcoder.pb.h"
#include "envoy/server/instance.h"

#include "common/config/well_known_names.h"

#include "server/config/network/http_connection_manager.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the gRPC JSON transcoder filter. @see NamedHttpFilterConfigFactory.
 */
class GrpcJsonTranscoderFilterConfig : public NamedHttpFilterConfigFactory {
public:
  HttpFilterFactoryCb createFilterFactory(const Json::Object& json_config,
                                          const std::string& stats_prefix,
                                          FactoryContext& context) override;
  HttpFilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                   const std::string& stats_prefix,
                                                   FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::api::v2::filter::http::GrpcJsonTranscoder()};
  }

  std::string name() override { return Config::HttpFilterNames::get().GRPC_JSON_TRANSCODER; };

private:
  HttpFilterFactoryCb
  createFilter(const envoy::api::v2::filter::http::GrpcJsonTranscoder& proto_config,
               const std::string& stats_prefix, FactoryContext& context);
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
