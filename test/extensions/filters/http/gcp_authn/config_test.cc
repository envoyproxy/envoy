#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.validate.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/extensions/filters/http/gcp_authn/filter_config.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthentication {

// TODO(tyxia) Update to use test fixture later
TEST(GcpAuthnFilterConfigTest, GcpAuthnFilterWithEmptyProto) {
  envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig filter_config;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  GcpAuthnFilterFactory factory;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(filter_config, "", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

} // namespace GcpAuthentication
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy