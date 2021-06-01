#pragma once

#include "envoy/extensions/common/matching/v3/extension_matcher.pb.validate.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Common {
namespace Http {
namespace MatchWrapper {

class MatchWrapperConfig : public Extensions::HttpFilters::Common::FactoryBase<
                               envoy::extensions::common::matching::v3::ExtensionWithMatcher> {
public:
  MatchWrapperConfig() : FactoryBase("match-wrapper") {}

private:
  Envoy::Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::common::matching::v3::ExtensionWithMatcher& proto_config,
      const std::string&, Server::Configuration::FactoryContext& context) override;
};

} // namespace MatchWrapper
} // namespace Http
} // namespace Common
} // namespace Envoy
