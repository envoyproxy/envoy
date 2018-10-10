#pragma once

#include "envoy/config/filter/http/transcoder/v2/transcoder.pb.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {

/**
 * Config registration for the gRPC JSON transcoder filter. @see NamedHttpFilterConfigFactory.
 */
class GrpcJsonTranscoderFilterConfig
    : public Common::FactoryBase<envoy::config::filter::http::transcoder::v2::GrpcJsonTranscoder> {
public:
  GrpcJsonTranscoderFilterConfig() : FactoryBase(HttpFilterNames::get().GrpcJsonTranscoder) {}

  Http::FilterFactoryCb
  createFilterFactory(const Json::Object& json_config, const std::string& stats_prefix,
                      Server::Configuration::FactoryContext& context) override;

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::http::transcoder::v2::GrpcJsonTranscoder& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
