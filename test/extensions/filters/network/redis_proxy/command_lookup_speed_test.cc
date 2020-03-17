// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include <chrono>
#include <string>
#include <vector>

#include "common/common/fmt.h"
#include "common/stats/isolated_store_impl.h"

#include "extensions/filters/network/common/redis/client_impl.h"
#include "extensions/filters/network/common/redis/supported_commands.h"
#include "extensions/filters/network/redis_proxy/command_splitter_impl.h"

#include "test/test_common/simulated_time_system.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

class NoOpSplitCallbacks : public CommandSplitter::SplitCallbacks {
public:
  NoOpSplitCallbacks() = default;
  ~NoOpSplitCallbacks() override = default;

  bool connectionAllowed() override { return true; }
  void onAuth(const std::string&) override {}
  void onResponse(Common::Redis::RespValuePtr&&) override {}
};

class NullRouterImpl : public Router {
  RouteSharedPtr upstreamPool(std::string&) override { return nullptr; }
};

class CommandLookUpSpeedTest {
public:
  void makeBulkStringArray(Common::Redis::RespValue& value,
                           const std::vector<std::string>& strings) {
    std::vector<Common::Redis::RespValue> values(strings.size());
    for (uint64_t i = 0; i < strings.size(); i++) {
      values[i].type(Common::Redis::RespType::BulkString);
      values[i].asString() = strings[i];
    }

    value.type(Common::Redis::RespType::Array);
    value.asArray().swap(values);
  }

  void makeRequests() {
    for (const std::string& command : Common::Redis::SupportedCommands::simpleCommands()) {
      Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
      makeBulkStringArray(*request, {command, "hello"});
      splitter_.makeRequest(std::move(request), callbacks_);
    }

    for (const std::string& command : Common::Redis::SupportedCommands::evalCommands()) {
      Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
      makeBulkStringArray(*request, {command, "hello"});
      splitter_.makeRequest(std::move(request), callbacks_);
    }
  }

  Router* router_{new NullRouterImpl()};
  Stats::IsolatedStoreImpl store_;
  Event::SimulatedTimeSystem time_system_;
  CommandSplitter::InstanceImpl splitter_{RouterPtr{router_}, store_, "redis.foo.", time_system_,
                                          false};
  NoOpSplitCallbacks callbacks_;
  CommandSplitter::SplitRequestPtr handle_;
};

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

static void BM_MakeRequests(benchmark::State& state) {
  Envoy::Extensions::NetworkFilters::RedisProxy::CommandLookUpSpeedTest context;

  for (auto _ : state) {
    context.makeRequests();
  }
}
BENCHMARK(BM_MakeRequests);
