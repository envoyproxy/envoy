#include <atomic>
#include <csignal>

#include "source/common/signal/non_fatal_signal_handler.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

std::atomic<int> g_count_a{0};
std::atomic<int> g_last_sig_a{0};
std::atomic<int> g_count_b{0};
std::atomic<int> g_count_c{0};

void handlerA(int sig, siginfo_t*, void*) {
  g_last_sig_a.store(sig, std::memory_order_relaxed);
  g_count_a.fetch_add(1, std::memory_order_relaxed);
}

void handlerB(int, siginfo_t*, void*) { g_count_b.fetch_add(1, std::memory_order_relaxed); }

void handlerC(int, siginfo_t*, void*) { g_count_c.fetch_add(1, std::memory_order_relaxed); }

void noopHandler(int, siginfo_t*, void*) {}

} // namespace

class NonFatalSignalHandlerTest : public ::testing::Test {
protected:
  void SetUp() override {
    g_count_a = 0;
    g_last_sig_a = 0;
    g_count_b = 0;
    g_count_c = 0;
  }

  void TearDown() override {
    for (auto cb : registered_) {
      NonFatalSignalHandler::removeNonFatalSignalHandler(cb);
    }
    registered_.clear();
  }

  void addHandler(NonFatalSignalCallback cb) {
    EXPECT_TRUE(NonFatalSignalHandler::registerNonFatalSignalHandler(cb));
    registered_.push_back(cb);
  }

  std::vector<NonFatalSignalCallback> registered_;
};

TEST_F(NonFatalSignalHandlerTest, RegisteredHandlerIsCalled) {
  addHandler(handlerA);
  NonFatalSignalHandler::callNonFatalSignalHandlers(SIGUSR2, nullptr, nullptr);
  EXPECT_EQ(g_count_a, 1);
  EXPECT_EQ(g_last_sig_a, SIGUSR2);
}

TEST_F(NonFatalSignalHandlerTest, RemovedHandlerIsNotCalled) {
  NonFatalSignalHandler::registerNonFatalSignalHandler(handlerA);
  NonFatalSignalHandler::removeNonFatalSignalHandler(handlerA);
  NonFatalSignalHandler::callNonFatalSignalHandlers(SIGUSR2, nullptr, nullptr);
  EXPECT_EQ(g_count_a, 0);
}

TEST_F(NonFatalSignalHandlerTest, MultipleHandlersAllCalled) {
  addHandler(handlerA);
  addHandler(handlerB);
  addHandler(handlerC);

  NonFatalSignalHandler::callNonFatalSignalHandlers(SIGUSR2, nullptr, nullptr);
  EXPECT_EQ(g_count_a, 1);
  EXPECT_EQ(g_count_b, 1);
  EXPECT_EQ(g_count_c, 1);
}

TEST_F(NonFatalSignalHandlerTest, MaxHandlersExceededReturnsFalse) {
  for (size_t i = 0; i < NonFatalSignalHandler::MaxHandlers; i++) {
    addHandler(noopHandler);
  }
  EXPECT_FALSE(NonFatalSignalHandler::registerNonFatalSignalHandler(handlerA));
}

TEST_F(NonFatalSignalHandlerTest, OnlyRemovedHandlerIsSkipped) {
  addHandler(handlerA);
  NonFatalSignalHandler::registerNonFatalSignalHandler(handlerB);
  addHandler(handlerC);

  NonFatalSignalHandler::removeNonFatalSignalHandler(handlerB);
  NonFatalSignalHandler::callNonFatalSignalHandlers(SIGUSR2, nullptr, nullptr);

  EXPECT_EQ(g_count_a, 1);
  EXPECT_EQ(g_count_b, 0);
  EXPECT_EQ(g_count_c, 1);
}

TEST(NonFatalSignalHandlerInstallTest, InstalledOnFirstRegistrationRemovedOnLast) {
  ASSERT_FALSE(NonFatalSignalHandler::isInstalled());
  NonFatalSignalHandler::registerNonFatalSignalHandler(handlerA);
  EXPECT_TRUE(NonFatalSignalHandler::isInstalled());

  NonFatalSignalHandler::registerNonFatalSignalHandler(handlerB);
  EXPECT_TRUE(NonFatalSignalHandler::isInstalled());

  NonFatalSignalHandler::removeNonFatalSignalHandler(handlerA);
  EXPECT_TRUE(NonFatalSignalHandler::isInstalled());
  NonFatalSignalHandler::removeNonFatalSignalHandler(handlerB);
  EXPECT_FALSE(NonFatalSignalHandler::isInstalled());
}

TEST(NonFatalSignalHandlerInstallTest, SIGUSR2DispatchesToRegisteredHandlers) {
  g_count_a = 0;
  g_last_sig_a = 0;

  NonFatalSignalHandler::registerNonFatalSignalHandler(handlerA);
  raise(SIGUSR2);
  NonFatalSignalHandler::removeNonFatalSignalHandler(handlerA);

  EXPECT_GE(g_count_a.load(), 1);
  EXPECT_EQ(g_last_sig_a.load(), SIGUSR2);
}

} // namespace Envoy
