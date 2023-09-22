// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include <chrono>
#include <string>
#include <vector>

#include "source/common/common/fmt.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/filters/network/common/redis/client_impl.h"
#include "source/extensions/filters/network/common/redis/supported_commands.h"
#include "source/extensions/filters/network/redis_proxy/command_splitter_impl.h"
#include "source/extensions/filters/network/redis_proxy/router_impl.h"

#include "test/test_common/simulated_time_system.h"

#include "absl/types/variant.h"
#include "benchmark/benchmark.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

class CommandSplitSpeedTest {
public:
  Common::Redis::RespValueSharedPtr
  makeSharedBulkStringArray(uint64_t batch_size, uint64_t key_size, uint64_t value_size) {
    Common::Redis::RespValueSharedPtr request{new Common::Redis::RespValue()};
    std::vector<Common::Redis::RespValue> values(batch_size * 2 + 1);
    values[0].type(Common::Redis::RespType::BulkString);
    values[0].asString() = "mset";
    for (uint64_t i = 1; i < batch_size * 2 + 1; i += 2) {
      values[i].type(Common::Redis::RespType::BulkString);
      values[i].asString() = std::string(key_size, 'k');
      values[i + 1].type(Common::Redis::RespType::BulkString);
      values[i + 1].asString() = std::string(value_size, 'v');
    }

    request->type(Common::Redis::RespType::Array);
    request->asArray().swap(values);

    return request;
  }
  using ValueOrPointer =
      absl::variant<const Common::Redis::RespValue, Common::Redis::RespValueConstSharedPtr>;

  void createShared(Common::Redis::RespValueSharedPtr request) {
    for (uint64_t i = 1; i < request->asArray().size(); i += 2) {
      auto single_set = std::make_shared<const Common::Redis::RespValue>(
          request, Common::Redis::Utility::SetRequest::instance(), i, i + 1);
    }
  }

  void createVariant(Common::Redis::RespValueSharedPtr request) {
    for (uint64_t i = 1; i < request->asArray().size(); i += 2) {
      Common::Redis::RespValue single_set(request, Common::Redis::Utility::SetRequest::instance(),
                                          i, i + 1);
      ValueOrPointer variant(single_set);
    }
  }

  void createLocalCompositeArray(Common::Redis::RespValueSharedPtr& request) {
    for (uint64_t i = 1; i < request->asArray().size(); i += 2) {
      Common::Redis::RespValue single_set(request, Common::Redis::Utility::SetRequest::instance(),
                                          i, i + 1);
    }
  }

  void copy(Common::Redis::RespValueSharedPtr& request) {
    std::vector<Common::Redis::RespValue> values(3);
    values[0].type(Common::Redis::RespType::BulkString);
    values[0].asString() = "set";
    values[1].type(Common::Redis::RespType::BulkString);
    values[2].type(Common::Redis::RespType::BulkString);
    Common::Redis::RespValue single_mset;
    single_mset.type(Common::Redis::RespType::Array);
    single_mset.asArray().swap(values);

    for (uint64_t i = 1; i < request->asArray().size(); i += 2) {
      single_mset.asArray()[1].asString() = request->asArray()[i].asString();
      single_mset.asArray()[2].asString() = request->asArray()[i + 1].asString();
    }
  }
};
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

static void bmSplitCompositeArray(benchmark::State& state) {
  Envoy::Extensions::NetworkFilters::RedisProxy::CommandSplitSpeedTest context;
  Envoy::Extensions::NetworkFilters::Common::Redis::RespValueSharedPtr request =
      context.makeSharedBulkStringArray(state.range(0), 36, state.range(1));
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    context.createLocalCompositeArray(request);
  }
}
BENCHMARK(bmSplitCompositeArray)->Ranges({{1, 100}, {64, 8 << 14}});

static void bmSplitCopy(benchmark::State& state) {
  Envoy::Extensions::NetworkFilters::RedisProxy::CommandSplitSpeedTest context;
  Envoy::Extensions::NetworkFilters::Common::Redis::RespValueSharedPtr request =
      context.makeSharedBulkStringArray(state.range(0), 36, state.range(1));
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    context.copy(request);
  }
}
BENCHMARK(bmSplitCopy)->Ranges({{1, 100}, {64, 8 << 14}});

static void bmSplitCreateShared(benchmark::State& state) {
  Envoy::Extensions::NetworkFilters::RedisProxy::CommandSplitSpeedTest context;
  Envoy::Extensions::NetworkFilters::Common::Redis::RespValueSharedPtr request =
      context.makeSharedBulkStringArray(state.range(0), 36, state.range(1));
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    context.createShared(request);
  }
  state.counters["use_count"] = request.use_count();
}
BENCHMARK(bmSplitCreateShared)->Ranges({{1, 100}, {64, 8 << 14}});

static void bmSplitCreateVariant(benchmark::State& state) {
  Envoy::Extensions::NetworkFilters::RedisProxy::CommandSplitSpeedTest context;
  Envoy::Extensions::NetworkFilters::Common::Redis::RespValueSharedPtr request =
      context.makeSharedBulkStringArray(state.range(0), 36, state.range(1));
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    context.createVariant(request);
  }
  state.counters["use_count"] = request.use_count();
}
BENCHMARK(bmSplitCreateVariant)->Ranges({{1, 100}, {64, 8 << 14}});
