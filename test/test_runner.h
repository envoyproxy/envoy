#pragma once

#include "common/common/logger.h"
#include "common/common/logger_delegates.h"
#include "common/common/thread.h"
#include "common/event/libevent.h"

#include "test/mocks/access_log/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_base.h"

#include "gmock/gmock.h"

namespace Envoy {
class TestRunner {
public:
  static int RunTests(int argc, char** argv) {
    ::testing::InitGoogleMock(&argc, argv);
    Event::Libevent::Global::initialize();

    // Use the recommended, but not default, "threadsafe" style for the Death Tests.
    // See: https://github.com/google/googletest/commit/84ec2e0365d791e4ebc7ec249f09078fb5ab6caa
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";

    // Set gtest properties
    // (https://github.com/google/googletest/blob/master/googletest/docs/AdvancedGuide.md#logging-additional-information),
    // they are available in the test XML.
    // TODO(htuch): Log these as well?
    TestBase::RecordProperty("TemporaryDirectory", TestEnvironment::temporaryDirectory());
    TestBase::RecordProperty("RunfilesDirectory", TestEnvironment::runfilesDirectory());

    TestEnvironment::setEnvVar("TEST_UDSDIR", TestEnvironment::unixDomainSocketDirectory(), 1);

    TestEnvironment::initializeOptions(argc, argv);
    Thread::MutexBasicLockable lock;
    Logger::Context logging_state(TestEnvironment::getOptions().logLevel(),
                                  TestEnvironment::getOptions().logFormat(), lock);

    // Allocate fake log access manager.
    testing::NiceMock<AccessLog::MockAccessLogManager> access_log_manager;
    std::unique_ptr<Logger::FileSinkDelegate> file_logger;

    // Redirect all logs to fake file when --log-path arg is specified in command line.
    if (!TestEnvironment::getOptions().logPath().empty()) {
      file_logger = std::make_unique<Logger::FileSinkDelegate>(
          TestEnvironment::getOptions().logPath(), access_log_manager, Logger::Registry::getSink());
    }
    int exit_status = RUN_ALL_TESTS();

    // Check that all singletons have been destroyed.
    TestBase::checkSingletonQuiescensce();

    return exit_status;
  }
};
} // namespace Envoy
