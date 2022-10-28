#include "test/mocks/server/factory_context.h"

#include "contrib/dynamo/filters/http/source/config.h"
#include "contrib/envoy/extensions/filters/http/dynamo/v3/dynamo.pb.h"
#include "contrib/envoy/extensions/filters/http/dynamo/v3/dynamo.pb.validate.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Dynamo {
namespace {

TEST(DynamoFilterConfigTest, DynamoFilter) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  DynamoFilterConfig factory;
  envoy::extensions::filters::http::dynamo::v3::Dynamo proto_config;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

} // namespace
} // namespace Dynamo
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
