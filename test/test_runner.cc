#include "test/test_runner.h"

#include <regex>

#include "common/common/logger.h"
#include "common/common/logger_delegates.h"
#include "common/common/thread.h"
#include "common/event/libevent.h"
#include "common/runtime/runtime_features.h"

#include "exe/process_wide.h"

#include "server/backtrace.h"

#include "test/common/runtime/utility.h"
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
  RuntimeManagingListener(std::string& runtime_override) : runtime_override_(runtime_override) {}

  // On each test start, edit RuntimeFeaturesDefaults with our custom runtime defaults.
  void OnTestStart(const ::testing::TestInfo&) override {
    if (!runtime_override_.empty()) {
      if (!Runtime::RuntimeFeaturesPeer::addFeature(runtime_override_)) {
        // If the entry was already in the hash map, don't remove it OnTestEnd.
        runtime_override_.clear();
      }
    }
  }

  // As each test ends, clean up the RuntimeFeaturesDefaults state.
  void OnTestEnd(const ::testing::TestInfo&) override {
    if (!runtime_override_.empty()) {
      Runtime::RuntimeFeaturesPeer::removeFeature(runtime_override_);
    }
  }
  std::string runtime_override_;
};

} // namespace

int TestRunner::RunTests(int argc, char** argv) {
  ::testing::InitGoogleMock(&argc, argv);
  // We hold on to process_wide to provide RAII cleanup of process-wide
  // state.
  ProcessWide process_wide;
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
  const std::regex PATTERN{"--runtime-feature-override-for-tests=(.*)", std::regex::optimize};
  std::string runtime_override = findAndRemove(PATTERN, argc, argv);
  if (!runtime_override.empty()) {
    ENVOY_LOG_TO_LOGGER(Logger::Registry::getLog(Logger::Id::testing), info,
                        "Running with runtime feature override {}", runtime_override);
    // Set up a listener which will create a global runtime and set the feature
    // to true for the duration of each test instance.
    ::testing::TestEventListeners& listeners = ::testing::UnitTest::GetInstance()->listeners();
    listeners.Append(new RuntimeManagingListener(runtime_override));
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
  Logger::Context logging_state(options.logLevel(), options.logFormat(), lock, false);

  // Allocate fake log access manager.
  testing::NiceMock<AccessLog::MockAccessLogManager> access_log_manager;
  std::unique_ptr<Logger::FileSinkDelegate> file_logger;

  // Redirect all logs to fake file when --log-path arg is specified in command line.
  if (!TestEnvironment::getOptions().logPath().empty()) {
    file_logger = std::make_unique<Logger::FileSinkDelegate>(
        TestEnvironment::getOptions().logPath(), access_log_manager, Logger::Registry::getSink());
  }
  return RUN_ALL_TESTS();
}

} // namespace Envoy
