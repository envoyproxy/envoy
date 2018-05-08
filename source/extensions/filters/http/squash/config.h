#pragma once

#include "envoy/config/filter/http/squash/v2/squash.pb.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Squash {

/**
 * Config registration for the squash filter. @see NamedHttpFilterConfigFactory.
 */
class SquashFilterConfigFactory : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  Http::FilterFactoryCb
  createFilterFactory(const Json::Object& json_config, const std::string&,
                      Server::Configuration::FactoryContext& context) override;

  Http::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config, const std::string&,
                               Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::config::filter::http::squash::v2::Squash()};
  }

  std::string name() override { return HttpFilterNames::get().SQUASH; }

private:
  Http::FilterFactoryCb
  createFilter(const envoy::config::filter::http::squash::v2::Squash& proto_config,
               Server::Configuration::FactoryContext& context);
};

} // namespace Squash
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
