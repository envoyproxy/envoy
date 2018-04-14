#pragma once

#include "envoy/config/filter/http/gzip/v2/gzip.pb.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Gzip {

/**
 * Config registration for the gzip filter. @see NamedHttpFilterConfigFactory.
 */
class GzipFilterFactory : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  Server::Configuration::HttpFilterFactoryCb
  createFilterFactory(const Json::Object& json_config, const std::string& stat_prefix,
                      Server::Configuration::FactoryContext& context) override;
  Server::Configuration::HttpFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config, const std::string& stats_prefix,
                               Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::config::filter::http::gzip::v2::Gzip()};
  }

  std::string name() override { return HttpFilterNames::get().ENVOY_GZIP; }

private:
  Server::Configuration::HttpFilterFactoryCb
  createFilter(const envoy::config::filter::http::gzip::v2::Gzip& gzip);
};

} // namespace Gzip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
