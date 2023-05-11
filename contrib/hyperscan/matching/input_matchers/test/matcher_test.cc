#ifndef HYPERSCAN_DISABLED
#include "source/common/thread_local/thread_local_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/blocking_counter.h"
#include "contrib/hyperscan/matching/input_matchers/source/matcher.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace Hyperscan {

// Verify that we do not get TSAN or other errors when creating scratch in
// multithreading.
TEST(ThreadLocalTest, RaceScratchCreation) {
  Thread::ThreadFactory& thread_factory = Thread::threadFactoryForTest();

  hs_database_t* database;
  hs_compile_error_t* compile_err;
  hs_error_t err = hs_compile("hello", 0, HS_MODE_BLOCK, nullptr, &database, &compile_err);
  ASSERT(err == HS_SUCCESS);

  constexpr int num_threads = 100;
  std::vector<Thread::ThreadPtr> threads;
  threads.reserve(num_threads);
  ConditionalInitializer creation, wait;
  absl::BlockingCounter creates(num_threads);
  for (int i = 0; i < num_threads; ++i) {
    threads.push_back(thread_factory.createThread([&database, &creation, &wait, &creates]() {
      // Block each thread on waking up a common condition variable,
      // so we make it likely to race on creation.
      creation.wait();
      ScratchThreadLocal tls(database, nullptr);
      creates.DecrementCount();

      wait.wait();
    }));
  }
  creation.setReady();
  creates.Wait();

  wait.setReady();
  for (auto& thread : threads) {
    thread->join();
  }
}

// Verify that even if thread local is not initialized, matcher can work and create thread local
// afterwards.
TEST(ThreadLocalTest, NotInitialized) {
  std::vector<const char*> expressions{"^/asdf/.+"};
  std::vector<unsigned int> flags{0};
  std::vector<unsigned int> ids{0};

  Event::MockDispatcher dispatcher;
  ThreadLocal::MockInstance instance;
  EXPECT_CALL(instance, allocateSlot())
      .WillOnce(testing::Invoke(&instance, &ThreadLocal::MockInstance::allocateSlotMock));
  Matcher matcher(expressions, flags, ids, dispatcher, instance, false);
  // Simulate moving to another thread.
  instance.data_[0].reset();

  EXPECT_CALL(dispatcher, post(_));
  EXPECT_TRUE(matcher.match("/asdf/1"));
}

// Verify that comparing works correctly for bounds.
TEST(BoundTest, Compare) {
  EXPECT_LT(Bound(1, 1), Bound(2, 1));
  EXPECT_LT(Bound(1, 2), Bound(2, 1));
  EXPECT_LT(Bound(2, 2), Bound(2, 1));
}

class MatcherTest : public ::testing::Test {
protected:
  void setup(const char* expression, unsigned int flag, bool report_start_of_matching) {
    std::vector<const char*> expressions{expression};
    std::vector<unsigned int> flags{flag};
    std::vector<unsigned int> ids{0};
    matcher_ = std::make_unique<Matcher>(expressions, flags, ids, dispatcher_, instance_,
                                         report_start_of_matching);
  }

  void TearDown() override {
    instance_.shutdownGlobalThreading();
    ::testing::Test::TearDown();
  }

  Event::MockDispatcher dispatcher_;
  ThreadLocal::InstanceImpl instance_;
  std::unique_ptr<Matcher> matcher_;
};

// Verify that matching will be performed successfully.
TEST_F(MatcherTest, Regex) {
  setup("^/asdf/.+", 0, false);

  EXPECT_TRUE(matcher_->match("/asdf/1"));
  EXPECT_FALSE(matcher_->match("/ASDF/1"));
  EXPECT_FALSE(matcher_->match("/asdf/\n"));
  EXPECT_FALSE(matcher_->match("\n/asdf/1"));
}

// Verify that matching will be performed successfully on empty optional value.
TEST_F(MatcherTest, Nullopt) {
  setup("^/asdf/.+", 0, false);

  EXPECT_FALSE(matcher_->match(absl::monostate()));
}

// Verify that matching will be performed case-insensitively.
TEST_F(MatcherTest, RegexWithCaseless) {
  setup("^/asdf/.+", HS_FLAG_CASELESS, false);

  EXPECT_TRUE(matcher_->match("/asdf/1"));
  EXPECT_TRUE(matcher_->match("/ASDF/1"));
  EXPECT_FALSE(matcher_->match("/asdf/\n"));
  EXPECT_FALSE(matcher_->match("\n/asdf/1"));
}

// Verify that matching a `.` will not exclude newlines.
TEST_F(MatcherTest, RegexWithDotAll) {
  setup("^/asdf/.+", HS_FLAG_DOTALL, false);

  EXPECT_TRUE(matcher_->match("/asdf/1"));
  EXPECT_FALSE(matcher_->match("/ASDF/1"));
  EXPECT_TRUE(matcher_->match("/asdf/\n"));
  EXPECT_FALSE(matcher_->match("\n/asdf/1"));
}

// Verify that `^` and `$` anchors match any newlines in data.
TEST_F(MatcherTest, RegexWithMultiline) {
  setup("^/asdf/.+", HS_FLAG_MULTILINE, false);

  EXPECT_TRUE(matcher_->match("/asdf/1"));
  EXPECT_FALSE(matcher_->match("/ASDF/1"));
  EXPECT_FALSE(matcher_->match("/asdf/\n"));
  EXPECT_TRUE(matcher_->match("\n/asdf/1"));
}

// Verify that expressions which can match against an empty string.
TEST_F(MatcherTest, RegexWithAllowEmpty) {
  setup(".*", HS_FLAG_ALLOWEMPTY, false);

  EXPECT_TRUE(matcher_->match(""));
}

// Verify that treating the pattern as a sequence of UTF-8 characters.
TEST_F(MatcherTest, RegexWithUTF8) {
  setup("^.$", HS_FLAG_UTF8, false);

  EXPECT_TRUE(matcher_->match("😀"));
}

// Verify that using Unicode properties for character classes.
TEST_F(MatcherTest, RegexWithUCP) {
  setup("^\\w$", HS_FLAG_UTF8 | HS_FLAG_UCP, false);

  EXPECT_TRUE(matcher_->match("Á"));
}

// Verify that using logical combination.
TEST_F(MatcherTest, RegexWithCombination) {
  std::vector<const char*> expressions{"a", "b", "1 | 2"};
  std::vector<unsigned int> flags{HS_FLAG_QUIET, HS_FLAG_QUIET, HS_FLAG_COMBINATION};
  std::vector<unsigned int> ids{1, 2, 0};

  matcher_ = std::make_unique<Matcher>(expressions, flags, ids, dispatcher_, instance_, false);

  EXPECT_TRUE(matcher_->match("a"));
}

// Verify that invalid expression will cause a throw.
TEST_F(MatcherTest, InvalidRegex) {
  EXPECT_THROW_WITH_MESSAGE(
      setup("(", 0, false), EnvoyException,
      "unable to compile pattern '(': Missing close parenthesis for group started at index 0.");
}

// Verify that replace all works correctly.
TEST_F(MatcherTest, ReplaceAll) {
  setup("b+", 0, true);

  EXPECT_EQ(matcher_->replaceAll("yabba dabba doo", "d"), "yada dada doo");
}

} // namespace Hyperscan
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
#endif
