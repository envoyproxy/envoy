#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route.pb.validate.h"

#include "common/router/config_impl.h"

#include "test/mocks/server/instance.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Router {
namespace {

using testing::NiceMock;
using testing::ReturnRef;

class RouteMatchBenchmark : public ::benchmark::Fixture {
public:
  RouteMatchBenchmark() : api_(Api::createApiForTest()) {
    ON_CALL(factory_context_, api()).WillByDefault(ReturnRef(*api_));
  }

  Api::ApiPtr api_;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
};

static envoy::config::route::v3::RouteConfiguration getBaseRouteConfig() {
  const std::string yaml = R"EOF(
virtual_hosts:
- name: default
  domains:
  - "*"
  routes:
  - match:
      prefix: "/shelves/[^\\/]+/books/matched"
    route:
      cluster: ats
)EOF";

  envoy::config::route::v3::RouteConfiguration route_config;
  TestUtility::loadFromYaml(yaml, route_config, true);
  TestUtility::validate(route_config);
  return route_config;
}

static Http::TestRequestHeaderMapImpl genMatchedHeaders() {
  return Http::TestRequestHeaderMapImpl{
      {":authority", "www.google.com"},   {":method", "GET"},
      {":path", "/shelves/1/books/matched"}, {"x-safe", "safe"},
      {"x-global-nope", "global"},        {"x-vhost-nope", "vhost"},
      {"x-route-nope", "route"},          {"x-forwarded-proto", "http"}};
}

BENCHMARK_DEFINE_F(RouteMatchBenchmark, RegexRouteTable)(benchmark::State& st) {
  const envoy::config::route::v3::RouteConfiguration route_config = getBaseRouteConfig();
  ConfigImpl config(route_config, factory_context_, ProtobufMessage::getNullValidationVisitor(),
                    true);

  for (auto _ : st) {
    config.route(genMatchedHeaders(), stream_info_, 0);
  }
}

BENCHMARK_REGISTER_F(RouteMatchBenchmark, RegexRouteTable)->Range(1, 1024)->Threads(1);

} // namespace
} // namespace Router
} // namespace Envoy
