#include "test/mocks/server/factory_context.h"

#include "contrib/checksum/filters/http/source/config.h"
#include "contrib/envoy/extensions/filters/http/checksum/v3alpha/checksum.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ChecksumFilter {
namespace {

TEST(ChecksumFilterConfigTest, ChecksumFilter) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  ChecksumFilterFactory factory;
  envoy::extensions::filters::http::checksum::v3alpha::ChecksumConfig proto_config;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

} // namespace
} // namespace ChecksumFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
