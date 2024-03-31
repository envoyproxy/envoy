#pragma once

#include <string>
#include <vector>

#include "source/common/stats/isolated_store_impl.h"
#include "source/exe/main_common.h"

#include "test/test_common/environment.h"

#include "absl/synchronization/notification.h"

namespace Envoy {

class MainCommonTestBase {
protected:
  MainCommonTestBase(Network::Address::IpVersion version);
  const char* const* argv();
  int argc();

  // Adds an argument, assuring that argv remains null-terminated.
  void addArg(const char* arg);

  // Adds options to make Envoy exit immediately after initialization.
  void initOnly();

  std::string config_file_;
  std::vector<const char*> argv_;
};

class AdminRequestTestBase : public MainCommonTestBase {
protected:
  AdminRequestTestBase(Network::Address::IpVersion version);

  // Runs an admin request specified in path, blocking until completion, and
  // returning the response body.
  std::string adminRequest(absl::string_view path, absl::string_view method);

  // Initiates Envoy running in its own thread.
  void startEnvoy();

  // Conditionally pauses at a critical point in the Envoy thread, waiting for
  // the test thread to trigger something at that exact line. The test thread
  // can then call resume_.Notify() to allow the Envoy thread to resume.
  void pauseResumeInterlock(bool enable);

  // Wait until Envoy is inside the main server run loop proper. Before entering, Envoy runs any
  // pending post callbacks, so it's not reliable to use adminRequest() or post() to do this.
  // Generally, tests should not depend on this for correctness, but as a result of
  // https://github.com/libevent/libevent/issues/779 we need to for TSAN. This is because the entry
  // to event_base_loop() is where the signal base race occurs, but once we're in that loop in
  // blocking mode, we're safe to take signals.
  // TODO(htuch): Remove when https://github.com/libevent/libevent/issues/779 is fixed.
  void waitForEnvoyRun();

  // Having triggered Envoy to quit (via signal or /quitquitquit), this blocks until Envoy exits.
  bool waitForEnvoyToExit();

  // Sends a quit request to the server, and waits for Envoy to exit. Returns
  // true if successful.
  bool quitAndWait();

  Stats::IsolatedStoreImpl stats_store_;
  std::unique_ptr<Thread::Thread> envoy_thread_;
  std::unique_ptr<MainCommon> main_common_;
  absl::Notification started_;
  absl::Notification finished_;
  absl::Notification resume_;
  absl::Notification pause_point_;
  bool envoy_return_{false};
  bool envoy_started_{false};
  bool envoy_finished_{false};
  bool pause_before_run_{false};
  bool pause_after_run_{false};
};

} // namespace Envoy
