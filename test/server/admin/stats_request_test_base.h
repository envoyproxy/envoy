#pragma once

#include "source/common/buffer/buffer_impl.h"
#include "source/common/stats/thread_local_store.h"
#include "source/server/admin/grouped_stats_request.h"
#include "source/server/admin/ungrouped_stats_request.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

template <class T> class StatsRequestTestBase : public testing::Test {
protected:
  StatsRequestTestBase();
  ~StatsRequestTestBase() override;

  uint32_t iterateChunks(T& request, bool drain = true, Http::Code expect_code = Http::Code::OK);

  Stats::StatName makeStatName(absl::string_view name) { return pool_.add(name); }

  std::string response(T& request);

  Stats::SymbolTableImpl symbol_table_;
  Stats::StatNamePool pool_;
  Stats::AllocatorImpl alloc_;
  testing::NiceMock<Stats::MockSink> sink_;
  testing::NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  testing::NiceMock<ThreadLocal::MockInstance> tls_;
  Stats::ThreadLocalStoreImpl store_;
  Buffer::OwnedImpl response_;
};

} // namespace Server
} // namespace Envoy
