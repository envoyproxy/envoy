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

#include "test/extensions/filters/network/redis_proxy/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/simulated_time_system.h"

#include "benchmark/benchmark.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

class NoOpSplitCallbacks : public CommandSplitter::SplitCallbacks {
public:
  NoOpSplitCallbacks() = default;
  ~NoOpSplitCallbacks() override = default;

  bool connectionAllowed() override { return true; }
  void onQuit() override {}
  void onAuth(const std::string&) override {}
  void onAuth(const std::string&, const std::string&) override {}
  void onResponse(Common::Redis::RespValuePtr&&) override {}
  Common::Redis::Client::Transaction& transaction() override { return transaction_; }

private:
  Common::Redis::Client::NoOpTransaction transaction_;
};

class NullRouterImpl : public Router {
  RouteSharedPtr upstreamPool(std::string&, const StreamInfo::StreamInfo&) override {
    return nullptr;
  }
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
      splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
    }

    for (const std::string& command : Common::Redis::SupportedCommands::evalCommands()) {
      Common::Redis::RespValuePtr request{new Common::Redis::RespValue()};
      makeBulkStringArray(*request, {command, "hello"});
      splitter_.makeRequest(std::move(request), callbacks_, dispatcher_, stream_info_);
    }
  }

  Router* router_{new NullRouterImpl()};
  Stats::IsolatedStoreImpl store_;
  Event::SimulatedTimeSystem time_system_;
  NiceMock<MockFaultManager> fault_manager_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  CommandSplitter::InstanceImpl splitter_{
      RouterPtr{router_},
      *store_.rootScope(),
      "redis.foo.",
      time_system_,
      false,
      std::make_unique<NiceMock<MockFaultManager>>(fault_manager_)};
  NoOpSplitCallbacks callbacks_;
  CommandSplitter::SplitRequestPtr handle_;
};

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

static void bmMakeRequests(benchmark::State& state) {
  Envoy::Extensions::NetworkFilters::RedisProxy::CommandLookUpSpeedTest context;

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    context.makeRequests();
  }
}
BENCHMARK(bmMakeRequests);
