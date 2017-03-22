#include "server/options_impl.h"

#include "common/common/logger.h"
#include "common/common/thread.h"
#include "common/event/libevent.h"
#include "common/ssl/openssl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#ifndef BAZEL_BRINGUP
#include "test/integration/integration.h"
#endif

int main(int argc, char** argv) {
  ::testing::InitGoogleMock(&argc, argv);
  Ssl::OpenSsl::initialize();
  Event::Libevent::Global::initialize();

  OptionsImpl options(argc, argv, "1", spdlog::level::err);
  Thread::MutexBasicLockable lock;
  Logger::Registry::initialize(options.logLevel(), lock);
#ifndef BAZEL_BRINGUP
  BaseIntegrationTest::default_log_level_ =
      static_cast<spdlog::level::level_enum>(options.logLevel());
#endif

  return RUN_ALL_TESTS();
}
