#include "test/exe/main_common_test_base.h"

#include "source/common/common/thread.h"

#include "test/test_common/thread_factory_for_test.h"

namespace Envoy {

MainCommonTestBase::MainCommonTestBase(Network::Address::IpVersion version)
    : config_file_(TestEnvironment::temporaryFileSubstitute(
          "test/config/integration/google_com_proxy_port_0.yaml", TestEnvironment::ParamMap(),
          TestEnvironment::PortMap(), version)),
      argv_({"envoy-static", "--use-dynamic-base-id", "-c", config_file_.c_str(), nullptr}) {}

const char* const* MainCommonTestBase::argv() { return &argv_[0]; }
int MainCommonTestBase::argc() { return argv_.size() - 1; }

// Adds an argument, assuring that argv remains null-terminated.
void MainCommonTestBase::addArg(const char* arg) {
  ASSERT(!argv_.empty());
  const size_t last = argv_.size() - 1;
  ASSERT(argv_[last] == nullptr); // invariant established in ctor, maintained below.
  argv_[last] = arg;              // guaranteed non-empty
  argv_.push_back(nullptr);
}

// Adds options to make Envoy exit immediately after initialization.
void MainCommonTestBase::initOnly() {
  addArg("--mode");
  addArg("init_only");
}

AdminRequestTestBase::AdminRequestTestBase(Network::Address::IpVersion version)
    : MainCommonTestBase(version) {
  addArg("--disable-hot-restart");
}

// Runs an admin request specified in path, blocking until completion, and
// returning the response body.
std::string AdminRequestTestBase::adminRequest(absl::string_view path, absl::string_view method) {
  absl::Notification done;
  std::string out;
  main_common_->adminRequest(
      path, method,
      [&done, &out](const Http::HeaderMap& /*response_headers*/, absl::string_view body) {
        out = std::string(body);
        done.Notify();
      });
  done.WaitForNotification();
  return out;
}

// Initiates Envoy running in its own thread.
void AdminRequestTestBase::startEnvoy() {
  envoy_thread_ = Thread::threadFactoryForTest().createThread([this]() {
    // Note: main_common_ is accessed in the testing thread, but
    // is race-free, as MainCommon::run() does not return until
    // triggered with an adminRequest POST to /quitquitquit, which
    // is done in the testing thread.
    main_common_ = std::make_unique<MainCommon>(argc(), argv());
    envoy_started_ = true;
    started_.Notify();
    pauseResumeInterlock(pause_before_run_);
    bool status = main_common_->run();
    pauseResumeInterlock(pause_after_run_);
    main_common_.reset();
    envoy_finished_ = true;
    envoy_return_ = status;
    finished_.Notify();
  });
}

// Conditionally pauses at a critical point in the Envoy thread, waiting for
// the test thread to trigger something at that exact line. The test thread
// can then call resume_.Notify() to allow the Envoy thread to resume.
void AdminRequestTestBase::pauseResumeInterlock(bool enable) {
  if (enable) {
    pause_point_.Notify();
    resume_.WaitForNotification();
  }
}

// Wait until Envoy is inside the main server run loop proper. Before entering, Envoy runs any
// pending post callbacks, so it's not reliable to use adminRequest() or post() to do this.
// Generally, tests should not depend on this for correctness, but as a result of
// https://github.com/libevent/libevent/issues/779 we need to for TSAN. This is because the entry
// to event_base_loop() is where the signal base race occurs, but once we're in that loop in
// blocking mode, we're safe to take signals.
// TODO(htuch): Remove when https://github.com/libevent/libevent/issues/779 is fixed.
void AdminRequestTestBase::waitForEnvoyRun() {
  absl::Notification done;
  main_common_->dispatcherForTest().post([this, &done] {
    struct Sacrifice : Event::DeferredDeletable {
      Sacrifice(absl::Notification& notify) : notify_(notify) {}
      ~Sacrifice() override { notify_.Notify(); }
      absl::Notification& notify_;
    };
    auto sacrifice = std::make_unique<Sacrifice>(done);
    // Wait for a deferred delete cleanup, this only happens in the main server run loop.
    main_common_->dispatcherForTest().deferredDelete(std::move(sacrifice));
  });
  done.WaitForNotification();
}

// Having triggered Envoy to quit (via signal or /quitquitquit), this blocks until Envoy exits.
bool AdminRequestTestBase::waitForEnvoyToExit() {
  finished_.WaitForNotification();
  envoy_thread_->join();
  return envoy_return_;
}

bool AdminRequestTestBase::quitAndWait() {
  adminRequest("/quitquitquit", "POST");
  return waitForEnvoyToExit();
}

} // namespace Envoy
