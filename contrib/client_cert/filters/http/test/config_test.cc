#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "contrib/client_cert/filters/http/source/config.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ClientCert {
namespace {

TEST(ClientCertFilterConfigTest, FactoryRegistration) {
  auto* factory =
      Registry::FactoryRegistry<Server::Configuration::NamedHttpFilterConfigFactory>::getFactory(
          "envoy.filters.http.client_cert");
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->name(), "envoy.filters.http.client_cert");
}

TEST(ClientCertFilterConfigTest, CreateFilterFactory) {
  ClientCertFilterFactory factory;
  NiceMock<Server::Configuration::MockFactoryContext> context;

  envoy::extensions::filters::http::client_cert::v3alpha::ClientCertConfig proto_config;
  proto_config.set_set_client_cert_chain(true);

  Http::FilterFactoryCb filter_factory =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  ASSERT_NE(filter_factory, nullptr);

  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamDecoderFilter(testing::_));
  filter_factory(filter_callbacks);
}

} // namespace
} // namespace ClientCert
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
