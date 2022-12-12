#pragma once

#include <string>

#include "source/extensions/filters/http/common/factory_base.h"

#include "library/common/extensions/filters/http/test_logger/filter.h"
#include "library/common/extensions/filters/http/test_logger/filter.pb.h"
#include "library/common/extensions/filters/http/test_logger/filter.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TestLogger {

/**
 * Config registration for the TestLogger filter. @see NamedHttpFilterConfigFactory.
 */
class Factory
    : public Common::FactoryBase<envoymobile::extensions::filters::http::test_logger::TestLogger> {
public:
  Factory() : FactoryBase("test_logger") {}

private:
  Envoy::Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoymobile::extensions::filters::http::test_logger::TestLogger& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(Factory);

} // namespace TestLogger
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
