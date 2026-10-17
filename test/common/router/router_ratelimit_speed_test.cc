#include <string>

#include "source/common/router/router_ratelimit.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Router {
namespace {

void rateLimitHeaderPresence(benchmark::State& state) {
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info;
  envoy::config::route::v3::RateLimit::Action::HeaderValueMatch config;
  config.set_descriptor_key("client");
  config.set_descriptor_value("tagged");
  auto* matcher = config.add_headers();
  matcher->set_name("x-client-tag");
  matcher->set_present_match(true);
  HeaderValueMatchAction action(config, context);
  Http::TestRequestHeaderMapImpl headers;
  const Http::LowerCaseString name("x-client-tag");
  for (int i = 0; i < state.range(0); ++i) {
    headers.addCopy(name, std::string(64, 'a' + i));
  }
  const std::string local_cluster = "api-gateway";
  for (auto _ : state) {
    RateLimit::DescriptorEntry entry;
    benchmark::DoNotOptimize(action.populateDescriptor(entry, local_cluster, headers, stream_info));
    benchmark::DoNotOptimize(entry);
  }
}
BENCHMARK(rateLimitHeaderPresence)->Arg(0)->Arg(1)->Arg(4);
} // namespace
} // namespace Router
} // namespace Envoy
