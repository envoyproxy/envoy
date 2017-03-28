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
#include "test/test_common/environment.h"

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

  // Quick and dirty filtering and execution of --envoy_test_setup. Scripts run in this environment
  // have access to env vars such as TEST_TMPDIR. This script can be used for situations where
  // genrules are unsuitable for unit tests, since genrules don't have access to TEST_TMPDIR.
  // TODO(rlazarus): Consider adding a TestOptionsImpl subclass of OptionsImpl during the #613 work,
  // where we will be doing some additional command-line parsing in OptionsImpl anyway.
  const std::string envoy_test_setup_flag = "--envoy_test_setup";
  int i;
  for (i = 0; i < argc; ++i) {
    const std::string arg(argv[i]);
    if (arg.find(envoy_test_setup_flag) == 0) {
      if (::system(arg.substr(envoy_test_setup_flag.size() + 1).c_str()) != 0) {
        std::cerr << "Failed to execute setup script " << arg << std::endl;
        ::exit(1);
      }
      for (; i < argc - 1; ++i) {
        argv[i] = argv[i + 1];
      }
      --argc;
      break;
    }
  }

  OptionsImpl options(argc, argv, "1", spdlog::level::err);
  Thread::MutexBasicLockable lock;
  Logger::Registry::initialize(options.logLevel(), lock);
#ifndef BAZEL_BRINGUP
  BaseIntegrationTest::default_log_level_ =
      static_cast<spdlog::level::level_enum>(options.logLevel());
#endif

  return RUN_ALL_TESTS();
}
