#include <map>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/scoped_route.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/secret.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/macros.h"
#include "source/common/event/libevent.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/server/admin/config_dump_handler.h"
#include "source/server/admin/config_tracker_impl.h"

#include "test/benchmark/main.h"
#include "test/mocks/server/admin_stream.h"
#include "test/mocks/server/instance.h"
#include "test/test_common/proto_filler.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Server {

namespace {

struct ComponentSpec {
  absl::string_view name;
  const Protobuf::Message* dump;
  absl::string_view entries;
  ProtoFiller::AnyTypes any_types;
};

const std::vector<ComponentSpec>& componentSpecs() {
  CONSTRUCT_ON_FIRST_USE(
      std::vector<ComponentSpec>,
      ComponentSpec{"clusters",
                    &envoy::admin::v3::ClustersConfigDump::default_instance(),
                    "dynamic_active_clusters",
                    {{"cluster", &envoy::config::cluster::v3::Cluster::default_instance()}}},
      ComponentSpec{"listeners",
                    &envoy::admin::v3::ListenersConfigDump::default_instance(),
                    "dynamic_listeners",
                    {{"listener", &envoy::config::listener::v3::Listener::default_instance()}}},
      ComponentSpec{
          "routes",
          &envoy::admin::v3::RoutesConfigDump::default_instance(),
          "dynamic_route_configs",
          {{"route_config", &envoy::config::route::v3::RouteConfiguration::default_instance()}}},
      ComponentSpec{"route_scopes",
                    &envoy::admin::v3::ScopedRoutesConfigDump::default_instance(),
                    "dynamic_scoped_route_configs",
                    {{"scoped_route_configs",
                      &envoy::config::route::v3::ScopedRouteConfiguration::default_instance()}}},
      ComponentSpec{
          "secrets",
          &envoy::admin::v3::SecretsConfigDump::default_instance(),
          "dynamic_active_secrets",
          {{"secret",
            &envoy::extensions::transport_sockets::tls::v3::Secret::default_instance()}}});
}

const ComponentSpec& componentSpec(absl::string_view name) {
  for (const ComponentSpec& spec : componentSpecs()) {
    if (spec.name == name) {
      return spec;
    }
  }
  PANIC(absl::StrCat("no component named ", name));
}

ProtobufTypes::MessagePtr makeDump(const ComponentSpec& spec, uint32_t resources) {
  ProtobufTypes::MessagePtr dump(spec.dump->New());
  const Protobuf::FieldDescriptor& entries =
      *dump->GetDescriptor()->FindFieldByName(std::string(spec.entries));
  for (uint32_t i = 0; i < resources; i++) {
    ProtoFiller::fill(*dump->GetReflection()->AddMessage(dump.get(), &entries),
                      {.any_types = spec.any_types});
  }
  return dump;
}

using DumpCache = std::map<std::pair<absl::string_view, uint32_t>, ProtobufTypes::MessagePtr>;

DumpCache& dumpCache() { MUTABLE_CONSTRUCT_ON_FIRST_USE(DumpCache); }

const Protobuf::Message& dumpFor(const ComponentSpec& spec, uint32_t resources) {
  DumpCache& cache = dumpCache();
  const std::pair<absl::string_view, uint32_t> key{spec.name, resources};
  const auto cached = cache.find(key);
  if (cached != cache.end()) {
    return *cached->second;
  }
  return *cache.emplace(key, makeDump(spec, resources)).first->second;
}

class ConfigDumpSpeedTest {
public:
  ConfigDumpSpeedTest(absl::string_view component, uint32_t resources) {
    Event::Libevent::Global::initialize();
    const ComponentSpec& spec = componentSpec(component);
    const Protobuf::Message& dump = dumpFor(spec, resources);
    entry_ = tracker_.add(std::string(spec.name), [&dump](const Matchers::StringMatcher&) {
      ProtobufTypes::MessagePtr copy(dump.New());
      copy->CopyFrom(dump);
      return copy;
    });
  }

  ConfigDumpHandler& handler() { return handler_; }
  AdminStream& adminStream() { return admin_stream_; }

private:
  testing::NiceMock<MockInstance> server_;
  ConfigTrackerImpl tracker_;
  ConfigTracker::EntryOwnerPtr entry_;
  ConfigDumpHandler handler_{tracker_, server_};
  testing::NiceMock<MockAdminStream> admin_stream_;
};

} // namespace
} // namespace Server
} // namespace Envoy

namespace {

uint32_t resourceCount(const benchmark::State& state) {
  const auto resources = static_cast<uint32_t>(state.range(0));
  return Envoy::benchmark::skipExpensiveBenchmarks() ? std::min(resources, 100U) : resources;
}

void dumpSizes(benchmark::internal::Benchmark* benchmark) {
  benchmark->Arg(100)->Arg(1000)->Arg(10000)->Unit(benchmark::kMillisecond);
}

uint64_t dump(benchmark::State& state, Envoy::Server::ConfigDumpSpeedTest& test_context) {
  Envoy::Http::ResponseHeaderMapPtr headers = Envoy::Http::ResponseHeaderMapImpl::create();
  Envoy::Buffer::OwnedImpl chunk;
  uint64_t bytes = 0;
  for (auto _ : state) { // NOLINT
    Envoy::Server::Admin::RequestPtr request =
        test_context.handler().makeRequest(test_context.adminStream());
    request->start(*headers);

    uint64_t response_bytes = 0;
    for (bool more = true; more;) {
      more = request->nextChunk(chunk);

      state.PauseTiming();
      response_bytes += chunk.length();
      chunk.drain(chunk.length());
      state.ResumeTiming();
    }
    bytes += response_bytes;
  }
  return bytes;
}

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_ConfigDump(benchmark::State& state, absl::string_view component) {
  Envoy::Server::ConfigDumpSpeedTest test_context(component, resourceCount(state));
  state.SetBytesProcessed(dump(state, test_context));
}
BENCHMARK_CAPTURE(BM_ConfigDump, clusters, "clusters")->Apply(dumpSizes);
BENCHMARK_CAPTURE(BM_ConfigDump, listeners, "listeners")->Apply(dumpSizes);
BENCHMARK_CAPTURE(BM_ConfigDump, routes, "routes")->Apply(dumpSizes);
BENCHMARK_CAPTURE(BM_ConfigDump, route_scopes, "route_scopes")->Apply(dumpSizes);
BENCHMARK_CAPTURE(BM_ConfigDump, secrets, "secrets")->Apply(dumpSizes);

} // namespace
