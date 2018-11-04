#pragma once

#include "envoy/config/filter/network/find_and_replace/v2alpha1/find_and_replace.pb.h"
#include "envoy/config/filter/network/find_and_replace/v2alpha1/find_and_replace.pb.validate.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace FindAndReplace {

/**
 * Config registration for the FindAndReplace Filter. @see NamedNetworkFilterConfigFactory.
 */
class FindAndReplaceConfigFactory
    : public Common::FactoryBase<
          envoy::config::filter::network::find_and_replace::v2alpha1::FindAndReplace> {
public:
  FindAndReplaceConfigFactory() : FactoryBase(NetworkFilterNames::get().FindAndReplace) {}

  // // NamedNetworkFilterConfigFactory
  // Network::FilterFactoryCb
  // createFilterFactory(const Json::Object& json_config,
  //                     Server::Configuration::FactoryContext& context) override;

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::network::find_and_replace::v2alpha1::FindAndReplace&
          proto_config,
      Server::Configuration::FactoryContext&) override;
};

} // namespace FindAndReplace
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
