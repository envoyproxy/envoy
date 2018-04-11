#include "extensions/filters/http/grpc_json_transcoder/config.h"

#include "envoy/config/filter/http/transcoder/v2/transcoder.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/json/config_schemas.h"

#include "extensions/filters/http/grpc_json_transcoder/json_transcoder_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {

Server::Configuration::HttpFilterFactoryCb GrpcJsonTranscoderFilterConfig::createFilter(
    const envoy::config::filter::http::transcoder::v2::GrpcJsonTranscoder& proto_config,
    const std::string&, Server::Configuration::FactoryContext&) {
  ASSERT(!proto_config.proto_descriptor().empty());
  ASSERT(proto_config.services_size() > 0);

  JsonTranscoderConfigSharedPtr filter_config =
      std::make_shared<JsonTranscoderConfig>(proto_config);

  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<JsonTranscoderFilter>(*filter_config));
  };
}

Server::Configuration::HttpFilterFactoryCb GrpcJsonTranscoderFilterConfig::createFilterFactory(
    const Json::Object& json_config, const std::string& stat_prefix,
    Server::Configuration::FactoryContext& context) {
  envoy::config::filter::http::transcoder::v2::GrpcJsonTranscoder proto_config;
  Config::FilterJson::translateGrpcJsonTranscoder(json_config, proto_config);
  return createFilter(proto_config, stat_prefix, context);
}

Server::Configuration::HttpFilterFactoryCb
GrpcJsonTranscoderFilterConfig::createFilterFactoryFromProto(
    const Protobuf::Message& proto_config, const std::string& stat_prefix,
    Server::Configuration::FactoryContext& context) {
  return createFilter(
      MessageUtil::downcastAndValidate<
          const envoy::config::filter::http::transcoder::v2::GrpcJsonTranscoder&>(proto_config),
      stat_prefix, context);
}

/**
 * Static registration for the grpc transcoding filter. @see RegisterNamedHttpFilterConfigFactory.
 */
static Registry::RegisterFactory<GrpcJsonTranscoderFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
