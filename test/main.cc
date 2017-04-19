#include "common/common/logger.h"
#include "common/common/thread.h"
#include "common/event/libevent.h"
#include "common/ssl/openssl.h"

#include "test/test_common/environment.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

int main(int argc, char** argv) {
  ::testing::InitGoogleMock(&argc, argv);
  Ssl::OpenSsl::initialize();
  Event::Libevent::Global::initialize();

  // Set gtest properties
  // (https://github.com/google/googletest/blob/master/googletest/docs/AdvancedGuide.md#logging-additional-information),
  // they are available in the test XML.
  // TODO(htuch): Log these as well?
  ::testing::Test::RecordProperty("TemporaryDirectory", TestEnvironment::temporaryDirectory());
  ::testing::Test::RecordProperty("RunfilesDirectory", TestEnvironment::runfilesDirectory());

  if (::setenv("TEST_UDSDIR", TestEnvironment::unixDomainSocketDirectory().c_str(), 1) != 0) {
    ::perror("Failed to set temporary UDS directory.");
    ::exit(1);
  }

  TestEnvironment::initializeOptions(argc, argv);
  Thread::MutexBasicLockable lock;
  Logger::Registry::initialize(TestEnvironment::getOptions().logLevel(), lock);

  return RUN_ALL_TESTS();
}
