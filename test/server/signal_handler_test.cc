#include "server/signal_handler.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

class SignalHandlerTest : public testing::Test {
protected:
  SignalHandlerTest() {}

  void rejectsInvalidSignals() {
    EXPECT_FALSE(handler_->allows("SIGUNKNOWN"));
    EXPECT_FALSE(handler_->allows(""));
  }
  void acceptsAllStandardSignals() {
    EXPECT_TRUE(handler_->allows("SIGTERM"));
    EXPECT_TRUE(handler_->allows("SIGUSR1"));
    EXPECT_TRUE(handler_->allows("SIGHUP"));
  }

  std::unique_ptr<SignalHandler> handler_;
};

TEST_F(SignalHandlerTest, AllSignalsImplicit) {
  handler_ = std::make_unique<SignalHandler>();
  acceptsAllStandardSignals();
  rejectsInvalidSignals();
}

TEST_F(SignalHandlerTest, AllSignalsExplicit) {
  handler_ = std::make_unique<SignalHandler>("ALL");
  acceptsAllStandardSignals();
  rejectsInvalidSignals();
}

TEST_F(SignalHandlerTest, AllSignalsThree) {
  handler_ = std::make_unique<SignalHandler>("SIGTERM,SIGUSR1,SIGHUP");
  acceptsAllStandardSignals();
  rejectsInvalidSignals();
}

TEST_F(SignalHandlerTest, SomeSignalsExplicit) {
  handler_ = std::make_unique<SignalHandler>("SIGTERM,SIGUSR1");

  EXPECT_TRUE(handler_->allows("SIGTERM"));
  EXPECT_TRUE(handler_->allows("SIGUSR1"));

  EXPECT_FALSE(handler_->allows("SIGHUP"));

  rejectsInvalidSignals();
}

TEST_F(SignalHandlerTest, NoneSignals) {
  handler_ = std::make_unique<SignalHandler>("NONE");
  EXPECT_FALSE(handler_->allows("SIGTERM"));
  EXPECT_FALSE(handler_->allows("SIGUSR1"));
  EXPECT_FALSE(handler_->allows("SIGHUP"));

  rejectsInvalidSignals();
}

} // namespace Server
} // namespace Envoy
