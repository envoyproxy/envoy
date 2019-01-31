// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "common/common/fmt.h"
#include "common/stats/isolated_store_impl.h"

#include "extensions/filters/network/redis_proxy/command_splitter_impl.h"
#include "extensions/filters/network/redis_proxy/supported_commands.h"

#include "test/test_common/printers.h"
#include "test/test_common/simulated_time_system.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

class NoOpSplitCallbacks : public CommandSplitter::SplitCallbacks {
public:
  NoOpSplitCallbacks() {}
  ~NoOpSplitCallbacks() {}

  void onResponse(RespValuePtr&&) override {}
};

class NullInstanceImpl : public ConnPool::Instance {
  ConnPool::PoolRequest* makeRequest(const std::string&, const RespValue&,
                                     ConnPool::PoolCallbacks&) override {
    return nullptr;
  }
};

class CommandLookUpSpeedTest {
public:
  void makeBulkStringArray(RespValue& value, const std::vector<std::string>& strings) {
    std::vector<RespValue> values(strings.size());
    for (uint64_t i = 0; i < strings.size(); i++) {
      values[i].type(RespType::BulkString);
      values[i].asString() = strings[i];
    }

    value.type(RespType::Array);
    value.asArray().swap(values);
  }

  void makeRequests() {
    RespValue request;
    for (const std::string& command : SupportedCommands::simpleCommands()) {
      makeBulkStringArray(request, {command, "hello"});
      splitter_.makeRequest(request, callbacks_);
    }

    for (const std::string& command : SupportedCommands::evalCommands()) {
      makeBulkStringArray(request, {command, "hello"});
      splitter_.makeRequest(request, callbacks_);
    }
  }

  ConnPool::Instance* conn_pool_{new NullInstanceImpl()};
  Stats::IsolatedStoreImpl store_;
  Event::SimulatedTimeSystem time_system_;
  CommandSplitter::InstanceImpl splitter_{ConnPool::InstancePtr{conn_pool_}, store_, "redis.foo.",
                                          time_system_};
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

// Boilerplate main(), which discovers benchmarks in the same file and runs them.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);

  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}
