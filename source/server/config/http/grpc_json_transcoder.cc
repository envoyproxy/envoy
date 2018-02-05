#include "server/config/http/grpc_json_transcoder.h"

#include "envoy/config/filter/http/transcoder/v2/transcoder.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/grpc/json_transcoder_filter.h"
#include "common/json/config_schemas.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb GrpcJsonTranscoderFilterConfig::createFilter(
    const envoy::config::filter::http::transcoder::v2::GrpcJsonTranscoder& proto_config,
    const std::string&, FactoryContext&) {
  ASSERT(!proto_config.proto_descriptor().empty());
  ASSERT(proto_config.services_size() > 0);

  Grpc::JsonTranscoderConfigSharedPtr filter_config =
      std::make_shared<Grpc::JsonTranscoderConfig>(proto_config);

  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(
        Http::StreamFilterSharedPtr{new Grpc::JsonTranscoderFilter(*filter_config)});
  };
}

HttpFilterFactoryCb GrpcJsonTranscoderFilterConfig::createFilterFactory(
    const Json::Object& json_config, const std::string& stat_prefix, FactoryContext& context) {
  envoy::config::filter::http::transcoder::v2::GrpcJsonTranscoder proto_config;
  Config::FilterJson::translateGrpcJsonTranscoder(json_config, proto_config);
  return createFilter(proto_config, stat_prefix, context);
}

HttpFilterFactoryCb
GrpcJsonTranscoderFilterConfig::createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                             const std::string& stat_prefix,
                                                             FactoryContext& context) {
  return createFilter(
      MessageUtil::downcastAndValidate<
          const envoy::config::filter::http::transcoder::v2::GrpcJsonTranscoder&>(proto_config),
      stat_prefix, context);
}

/**
 * Static registration for the grpc transcoding filter. @see RegisterNamedHttpFilterConfigFactory.
 */
static Registry::RegisterFactory<GrpcJsonTranscoderFilterConfig, NamedHttpFilterConfigFactory>
    register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
