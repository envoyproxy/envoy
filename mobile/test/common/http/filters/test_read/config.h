#pragma once

#include <string>

#include "source/extensions/filters/http/common/factory_base.h"

#include "test/common/http/filters/test_read/filter.pb.h"
#include "test/common/http/filters/test_read/filter.pb.validate.h"

namespace Envoy {
namespace HttpFilters {
namespace TestRead {

/**
 * Config registration for the TestRead filter. @see NamedHttpFilterConfigFactory.
 */
class TestReadFilterFactory
    : public Envoy::Extensions::HttpFilters::Common::UnifiedFactoryBase<
          envoymobile::test::integration::filters::http::test_read::TestRead> {
public:
  TestReadFilterFactory() : UnifiedFactoryBase("test_read") {}

private:
  absl::StatusOr<::Envoy::Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoymobile::test::integration::filters::http::test_read::TestRead& config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;
};

DECLARE_FACTORY(TestReadFilterFactory);

} // namespace TestRead
} // namespace HttpFilters
} // namespace Envoy
