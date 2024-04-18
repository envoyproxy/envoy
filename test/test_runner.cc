#include "test/test_runner.h"

#include <regex>

#include "source/common/common/logger.h"
#include "source/common/common/logger_delegates.h"
#include "source/common/common/thread.h"
#include "source/common/event/libevent.h"
#include "source/common/runtime/runtime_features.h"
#include "source/exe/process_wide.h"
#include "source/server/backtrace.h"

#include "test/mocks/access_log/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_listener.h"

#include "gmock/gmock.h"

namespace Envoy {

namespace {

std::string findAndRemove(const std::regex& pattern, int& argc, char**& argv) {
  std::smatch matched;
  std::string return_value;
  for (int i = 0; i < argc; ++i) {
    if (return_value.empty()) {
      std::string argument = std::string(argv[i]);
      if (regex_search(argument, matched, pattern)) {
        return_value = matched[1];
        argc--;
      }
    }
    if (!return_value.empty() && i < argc) {
      argv[i] = argv[i + 1];
    }
  }
  return return_value;
}

// This class is created iff a test is run with the special runtime override flag.
class RuntimeManagingListener : public ::testing::EmptyTestEventListener {
public:
  RuntimeManagingListener(std::string& runtime_override, bool disable = false)
      : runtime_override_(runtime_override), disable_(disable) {}

  // On each test start, edit RuntimeFeaturesDefaults with our custom runtime defaults.
  // The defaults will be restored by TestListener::OnTestEnd.
  void OnTestStart(const ::testing::TestInfo&) override {
    if (!runtime_override_.empty()) {
      bool old_value = Runtime::runtimeFeatureEnabled(runtime_override_);
      if (disable_ != old_value) {
        // If the entry was already in the hash map, don't invert it OnTestEnd.
        runtime_override_.clear();
      } else {
        Runtime::maybeSetRuntimeGuard(runtime_override_, !disable_);
      }
    }
  }

  std::string runtime_override_;
  // This marks whether the runtime feature was enabled by default and needs to be overridden to
  // false.
  bool disable_;
};

bool isDeathTestChild(int argc, char** argv) {
  for (int i = 0; i < argc; ++i) {
    if (absl::StartsWith(argv[i], "--gtest_internal_run_death_test")) {
      return true;
    }
  }
  return false;
}

} // namespace

int TestRunner::runTests(int argc, char** argv) {
  const bool is_death_test_child = isDeathTestChild(argc, argv);
  ::testing::InitGoogleMock(&argc, argv);
  // We hold on to process_wide to provide RAII cleanup of process-wide
  // state.
  ProcessWide process_wide(false);
  // Add a test-listener so we can call a hook where we can do a quiescence
  // check after each method. See
  // https://github.com/google/googletest/blob/master/googletest/docs/advanced.md
  // for details.
  ::testing::TestEventListeners& listeners = ::testing::UnitTest::GetInstance()->listeners();
  listeners.Append(new TestListener);

  // Use the recommended, but not default, "threadsafe" style for the Death Tests.
  // See: https://github.com/google/googletest/commit/84ec2e0365d791e4ebc7ec249f09078fb5ab6caa
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";

  // Set gtest properties
  // (https://github.com/google/googletest/blob/master/googletest/docs/advanced.md#logging-additional-information),
  // they are available in the test XML.
  // TODO(htuch): Log these as well?
  testing::Test::RecordProperty("TemporaryDirectory", TestEnvironment::temporaryDirectory());

  TestEnvironment::setEnvVar("TEST_UDSDIR", TestEnvironment::unixDomainSocketDirectory(), 1);

  // Before letting TestEnvironment latch argv and argc, remove any runtime override flag.
  // This allows doing test overrides of Envoy runtime features without adding
  // test flags to the Envoy production command line.
  const std::regex ENABLE_PATTERN{"--runtime-feature-override-for-tests=(.*)",
                                  std::regex::optimize};
  std::string runtime_override_enable = findAndRemove(ENABLE_PATTERN, argc, argv);
  if (!runtime_override_enable.empty()) {
    ENVOY_LOG_TO_LOGGER(Logger::Registry::getLog(Logger::Id::testing), info,
                        "Running with runtime feature override enable {}", runtime_override_enable);
    // Set up a listener which will create a global runtime and set the feature
    // to true for the duration of each test instance.
    ::testing::TestEventListeners& listeners = ::testing::UnitTest::GetInstance()->listeners();
    listeners.Append(new RuntimeManagingListener(runtime_override_enable));
  }
  const std::regex DISABLE_PATTERN{"--runtime-feature-disable-for-tests=(.*)",
                                   std::regex::optimize};
  std::string runtime_override_disable = findAndRemove(DISABLE_PATTERN, argc, argv);
  if (!runtime_override_disable.empty()) {
    ENVOY_LOG_TO_LOGGER(Logger::Registry::getLog(Logger::Id::testing), info,
                        "Running with runtime feature override disable {}",
                        runtime_override_disable);
    // Set up a listener which will create a global runtime and set the feature
    // to false for the duration of each test instance.
    ::testing::TestEventListeners& listeners = ::testing::UnitTest::GetInstance()->listeners();
    listeners.Append(new RuntimeManagingListener(runtime_override_disable, true));
  }

#ifdef ENVOY_CONFIG_COVERAGE
  // Coverage tests are run with -l trace --log-path /dev/null, in order to
  // ensure that all of the code-paths from the maximum level of tracing are
  // covered in tests, but we don't wind up filling up CI with useless detailed
  // artifacts.
  //
  // The downside of this is that if there's a crash, the backtrace is lost, as
  // the backtracing mechanism uses logging, so force the backtraces to stderr.
  BackwardsTrace::setLogToStderr(true);
#endif

  TestEnvironment::initializeOptions(argc, argv);
  Thread::MutexBasicLockable lock;

  Server::Options& options = TestEnvironment::getOptions();
  Logger::Context logging_state(options.logLevel(), options.logFormat(), lock, false,
                                options.enableFineGrainLogging());

  // Allocate fake log access manager.
  testing::NiceMock<AccessLog::MockAccessLogManager> access_log_manager;
  std::unique_ptr<Logger::FileSinkDelegate> file_logger;

  // Redirect all logs to fake file when --log-path arg is specified in command line.
  // However do not redirect to file from death test children as the parent typically
  // looks for specific output in stderr
  if (!TestEnvironment::getOptions().logPath().empty() && !is_death_test_child) {
    file_logger = std::make_unique<Logger::FileSinkDelegate>(
        TestEnvironment::getOptions().logPath(), access_log_manager, Logger::Registry::getSink());
  }

  // Reset all ENVOY_BUG counters.
  Envoy::Assert::resetEnvoyBugCountersForTest();

#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
  // Fuzz tests may run Envoy tests in fuzzing mode to generate corpora. In this case, we do not
  // want to fail building the fuzz test because of a failed test run, which can happen when testing
  // functionality in fuzzing test mode. Dependencies (like RE2) change behavior when in fuzzing
  // mode, so we do not want to rely on a behavior test when generating a corpus.
  (void)RUN_ALL_TESTS();
  return 0;
#else
  return RUN_ALL_TESTS();
#endif
}

} // namespace Envoy
