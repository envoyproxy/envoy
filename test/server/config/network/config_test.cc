#include "envoy/registry/registry.h"

#include "common/access_log/access_log_impl.h"
#include "common/config/filter_json.h"
#include "common/config/well_known_names.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/network/client_ssl_auth/config.h"
#include "extensions/filters/network/ext_authz/config.h"
#include "extensions/filters/network/http_connection_manager/config.h"
#include "extensions/filters/network/mongo_proxy/config.h"
#include "extensions/filters/network/ratelimit/config.h"
#include "extensions/filters/network/redis_proxy/config.h"
#include "extensions/filters/network/tcp_proxy/config.h"

#include "test/mocks/grpc/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;
using testing::_;

namespace Envoy {
namespace Server {
namespace Configuration {

// Negative test for protoc-gen-validate constraints.
// TODO(mattklein123): Break this test apart into per extension tests.
TEST(NetworkFilterConfigTest, ValidateFail) {
  NiceMock<MockFactoryContext> context;

  Extensions::NetworkFilters::ClientSslAuth::ClientSslAuthConfigFactory client_ssl_auth_factory;
  envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth client_ssl_auth_proto;
  Extensions::NetworkFilters::HttpConnectionManager::HttpConnectionManagerFilterConfigFactory
      hcm_factory;
  envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager hcm_proto;
  Extensions::NetworkFilters::MongoProxy::MongoProxyFilterConfigFactory mongo_factory;
  envoy::config::filter::network::mongo_proxy::v2::MongoProxy mongo_proto;
  Extensions::NetworkFilters::RateLimitFilter::RateLimitConfigFactory rate_limit_factory;
  envoy::config::filter::network::rate_limit::v2::RateLimit rate_limit_proto;
  Extensions::NetworkFilters::RedisProxy::RedisProxyFilterConfigFactory redis_factory;
  envoy::config::filter::network::redis_proxy::v2::RedisProxy redis_proto;
  Extensions::NetworkFilters::TcpProxy::TcpProxyConfigFactory tcp_proxy_factory;
  envoy::config::filter::network::tcp_proxy::v2::TcpProxy tcp_proxy_proto;
  Extensions::NetworkFilters::ExtAuthz::ExtAuthzConfigFactory ext_authz_factory;
  envoy::config::filter::network::ext_authz::v2::ExtAuthz ext_authz_proto;
  const std::vector<std::pair<NamedNetworkFilterConfigFactory&, Protobuf::Message&>> filter_cases =
      {
          {client_ssl_auth_factory, client_ssl_auth_proto},
          {ext_authz_factory, ext_authz_proto},
          {hcm_factory, hcm_proto},
          {mongo_factory, mongo_proto},
          {rate_limit_factory, rate_limit_proto},
          {redis_factory, redis_proto},
          {tcp_proxy_factory, tcp_proxy_proto},
      };

  for (const auto& filter_case : filter_cases) {
    EXPECT_THROW(filter_case.first.createFilterFactoryFromProto(filter_case.second, context),
                 ProtoValidationException);
  }
}

TEST(NetworkFilterConfigTest, DoubleRegistrationTest) {
  EXPECT_THROW_WITH_MESSAGE(
      (Registry::RegisterFactory<
          Extensions::NetworkFilters::ClientSslAuth::ClientSslAuthConfigFactory,
          NamedNetworkFilterConfigFactory>()),
      EnvoyException,
      fmt::format("Double registration for name: '{}'",
                  Config::NetworkFilterNames::get().CLIENT_SSL_AUTH));
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy
