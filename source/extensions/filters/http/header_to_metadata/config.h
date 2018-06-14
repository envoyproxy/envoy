#pragma once

#include "envoy/server/filter_config.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderToMetadataFilter {

/**
 * Config registration for the header-to-metadata filter. @see NamedHttpFilterConfigFactory.
 */
class HeaderToMetadataConfig : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  Http::FilterFactoryCb createFilterFactory(const Json::Object&, const std::string&,
                                            Server::Configuration::FactoryContext&) override;

  Http::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&, const std::string&,
                               Server::Configuration::FactoryContext&) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() override { return HttpFilterNames::get().HEADER_TO_METADATA; }
};

} // namespace HeaderToMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
