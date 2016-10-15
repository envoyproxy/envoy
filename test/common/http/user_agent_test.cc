#include "common/http/header_map_impl.h"
#include "common/http/user_agent.h"

#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

namespace Http {

TEST(UserAgentTest, All) {
  Stats::MockStore stat_store;
  Stats::MockTimespan span;

  EXPECT_CALL(stat_store.counter_, inc()).Times(4);
  EXPECT_CALL(stat_store, counter("test.user_agent.ios.downstream_cx_total"));
  EXPECT_CALL(stat_store, counter("test.user_agent.ios.downstream_rq_total"));
  EXPECT_CALL(stat_store, counter("test.user_agent.ios.downstream_cx_destroy_remote_active_rq"));
  EXPECT_CALL(span, complete("test.user_agent.ios.downstream_cx_length_ms"));

  {
    UserAgent ua;
    ua.initializeFromHeaders(TestHeaderMapImpl{{"user-agent", "aaa iOS bbb"}}, "test.", stat_store);
    ua.initializeFromHeaders(TestHeaderMapImpl{{"user-agent", "aaa android bbb"}}, "test.",
                             stat_store);
    ua.completeConnectionLength(span);
  }

  EXPECT_CALL(stat_store, counter("test.user_agent.android.downstream_cx_total"));
  EXPECT_CALL(stat_store, counter("test.user_agent.android.downstream_rq_total"));
  EXPECT_CALL(stat_store,
              counter("test.user_agent.android.downstream_cx_destroy_remote_active_rq"));
  EXPECT_CALL(span, complete("test.user_agent.android.downstream_cx_length_ms"));

  {
    UserAgent ua;
    ua.initializeFromHeaders(TestHeaderMapImpl{{"user-agent", "aaa android bbb"}}, "test.",
                             stat_store);
    ua.completeConnectionLength(span);
  }

  {
    UserAgent ua;
    ua.initializeFromHeaders(TestHeaderMapImpl{{"user-agent", "aaa bbb"}}, "test.", stat_store);
    ua.initializeFromHeaders(TestHeaderMapImpl{{"user-agent", "aaa android bbb"}}, "test.",
                             stat_store);
    ua.completeConnectionLength(span);
  }

  {
    UserAgent ua;
    ua.initializeFromHeaders(TestHeaderMapImpl{}, "test.", stat_store);
    ua.completeConnectionLength(span);
  }
}

} // Http
