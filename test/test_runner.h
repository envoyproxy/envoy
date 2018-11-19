#pragma once

#include "common/common/logger.h"
#include "common/common/logger_delegates.h"
#include "common/common/thread.h"
#include "common/event/libevent.h"

#include "test/mocks/access_log/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/global.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
class TestRunner {
public:
  static int RunTests(int argc, char** argv) {
    ::testing::InitGoogleMock(&argc, argv);
    Event::Libevent::Global::initialize();

    // Set gtest properties
    // (https://github.com/google/googletest/blob/master/googletest/docs/AdvancedGuide.md#logging-additional-information),
    // they are available in the test XML.
    // TODO(htuch): Log these as well?
    ::testing::Test::RecordProperty("TemporaryDirectory", TestEnvironment::temporaryDirectory());
    ::testing::Test::RecordProperty("RunfilesDirectory", TestEnvironment::runfilesDirectory());

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
    std::string active_singletons = Test::Globals::describeActiveSingletons();
    if (!active_singletons.empty()) {
      std::cerr << "\n\nFAIL: Active singletons exist:\n" << active_singletons << std::endl;
      exit_status = EXIT_FAILURE;
    }

    return exit_status;
  }
};
} // namespace Envoy
