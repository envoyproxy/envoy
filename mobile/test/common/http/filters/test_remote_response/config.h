#pragma once

#include <string>

#include "source/extensions/filters/http/common/factory_base.h"

#include "test/common/http/filters/test_remote_response/filter.pb.h"
#include "test/common/http/filters/test_remote_response/filter.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TestRemoteResponse {

/**
 * Config registration for the test_remote_response filter. @see NamedHttpFilterConfigFactory.
 */
class TestRemoteResponseFilterFactory
    : public Common::FactoryBase<
          envoymobile::extensions::filters::http::test_remote_response::TestRemoteResponse> {
public:
  TestRemoteResponseFilterFactory() : FactoryBase("test_remote_response") {}

private:
  ::Envoy::Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoymobile::extensions::filters::http::test_remote_response::TestRemoteResponse&
          config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(TestRemoteResponseFilterFactory);

} // namespace TestRemoteResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
