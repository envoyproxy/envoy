#pragma once

#include <string>

#include "source/extensions/filters/http/common/factory_base.h"

#include "test/common/http/filters/test_logger/filter.h"
#include "test/common/http/filters/test_logger/filter.pb.h"
#include "test/common/http/filters/test_logger/filter.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TestLogger {

/**
 * Config registration for the TestLogger filter. @see NamedHttpFilterConfigFactory.
 */
class Factory : public Common::UnifiedFactoryBase<
                    envoymobile::extensions::filters::http::test_logger::TestLogger> {
public:
  Factory() : UnifiedFactoryBase("test_logger") {}

private:
  absl::StatusOr<Envoy::Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoymobile::extensions::filters::http::test_logger::TestLogger& config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;
};

DECLARE_FACTORY(Factory);

} // namespace TestLogger
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
