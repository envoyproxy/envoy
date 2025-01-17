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
    : public Envoy::Extensions::HttpFilters::Common::FactoryBase<
          envoymobile::test::integration::filters::http::test_read::TestRead> {
public:
  TestReadFilterFactory() : FactoryBase("test_read") {}

private:
  ::Envoy::Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoymobile::test::integration::filters::http::test_read::TestRead& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(TestReadFilterFactory);

} // namespace TestRead
} // namespace HttpFilters
} // namespace Envoy
