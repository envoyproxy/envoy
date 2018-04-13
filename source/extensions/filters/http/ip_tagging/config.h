#pragma once

#include <string>

#include "envoy/config/filter/http/ip_tagging/v2/ip_tagging.pb.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace IpTagging {

/**
 * Config registration for the router filter. @see NamedHttpFilterConfigFactory.
 */
class IpTaggingFilterFactory : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  Server::Configuration::HttpFilterFactoryCb
  createFilterFactory(const Json::Object& json_config, const std::string& stat_prefix,
                      Server::Configuration::FactoryContext& context) override;

  Server::Configuration::HttpFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stat_prefix,
                               Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::config::filter::http::ip_tagging::v2::IPTagging()};
  }

  std::string name() override { return HttpFilterNames::get().IP_TAGGING; }
};

} // namespace IpTagging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
