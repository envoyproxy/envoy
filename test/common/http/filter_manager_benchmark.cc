#include <memory>
#include <optional>

#include "envoy/common/optref.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/matcher/matcher.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/http/filter_manager.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/common/stream_info/stream_info_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_reply/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/overload_manager.h"
#include "test/test_common/test_runtime.h"

#include "benchmark/benchmark.h"

using testing::NiceMock;

namespace Envoy {
namespace Http {
namespace {

struct FilterManagerBenchmarkContext {
  NiceMock<MockFilterManagerCallbacks> filter_manager_callbacks_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Network::MockConnection> connection_;
  Envoy::Http::MockFilterChainFactory filter_factory_;
  NiceMock<LocalReply::MockLocalReply> local_reply_;
  Protocol protocol_{Protocol::Http2};
  NiceMock<MockTimeSystem> time_source_;
  StreamInfo::FilterStateSharedPtr filter_state_ =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  NiceMock<Server::MockOverloadManager> overload_manager_;
};

static void BM_FilterManagerCreateDestroy(benchmark::State& state) {
  FilterManagerBenchmarkContext context;

  for (auto _ : state) {
    auto filter_manager = std::make_unique<DownstreamFilterManager>(
        context.filter_manager_callbacks_, context.dispatcher_, context.connection_, 0, nullptr,
        true, 10000, context.filter_factory_, context.local_reply_, context.protocol_,
        context.time_source_, context.filter_state_, context.overload_manager_);
    filter_manager->destroyFilters();
  }
}
BENCHMARK(BM_FilterManagerCreateDestroy);

} // namespace
} // namespace Http
} // namespace Envoy
