#pragma once

#include "source/common/buffer/buffer_impl.h"
#include "source/common/stats/thread_local_store.h"
#include "source/server/admin/stats_render.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

class StatsRenderTestBase : public testing::Test {
protected:
  StatsRenderTestBase();
  ~StatsRenderTestBase() override;

  template <class T>
  std::string render(StatsRender& render, absl::string_view name, const T& value) {
    render.generate(response_, std::string(name), value);
    render.finalize(response_);
    return response_.toString();
  }

  Stats::ParentHistogram& populateHistogram(const std::string& name,
                                            const std::vector<uint64_t>& vals);

  Stats::SymbolTableImpl symbol_table_;
  Stats::AllocatorImpl alloc_;
  testing::NiceMock<Stats::MockSink> sink_;
  testing::NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  testing::NiceMock<ThreadLocal::MockInstance> tls_;
  Stats::ThreadLocalStoreImpl store_;
  Http::TestResponseHeaderMapImpl response_headers_;
  Buffer::OwnedImpl response_;
  StatsParams params_;
};

} // namespace Server
} // namespace Envoy
