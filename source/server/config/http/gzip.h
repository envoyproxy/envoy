#pragma once

#include "envoy/config/filter/http/gzip/v2/gzip.pb.h"
#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the gzip filter. @see NamedHttpFilterConfigFactory.
 */
class GzipFilterConfig : public NamedHttpFilterConfigFactory {
public:
  HttpFilterFactoryCb createFilterFactory(const Json::Object& json_config,
                                          const std::string& stat_prefix,
                                          FactoryContext& context) override;
  HttpFilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message& config,
                                                   const std::string& stats_prefix,
                                                   FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::config::filter::http::gzip::v2::Gzip()};
  }

  std::string name() override { return Config::HttpFilterNames::get().ENVOY_GZIP; }

private:
  HttpFilterFactoryCb createFilter(const envoy::config::filter::http::gzip::v2::Gzip& gzip);
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
