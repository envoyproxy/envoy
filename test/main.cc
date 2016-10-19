#include "server/options_impl.h"

#include "common/common/logger.h"
#include "common/common/thread.h"
#include "common/event/libevent.h"
#include "common/ssl/openssl.h"

#include "test/integration/integration.h"

int main(int argc, char** argv) {
  ::testing::InitGoogleMock(&argc, argv);
  Ssl::OpenSsl::initialize();
  Event::Libevent::Global::initialize();

  OptionsImpl options(argc, argv, "1", spdlog::level::err);
  Thread::MutexBasicLockable lock;
  Logger::Registry::initialize(options.logLevel(), lock);
  BaseIntegrationTest::default_log_level_ =
      static_cast<spdlog::level::level_enum>(options.logLevel());

  return RUN_ALL_TESTS();
}
