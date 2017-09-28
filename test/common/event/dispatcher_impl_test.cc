#include <fcntl.h>

#include <functional>

#include "common/event/dispatcher_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Eq;
using testing::InSequence;
using testing::NiceMock;
using testing::Not;
using testing::Return;
using testing::SetArgReferee;
using testing::StrEq;
using testing::_;

namespace Envoy {
namespace Event {

class TestDeferredDeletable : public DeferredDeletable {
public:
  TestDeferredDeletable(std::function<void()> on_destroy) : on_destroy_(on_destroy) {}
  ~TestDeferredDeletable() { on_destroy_(); }

private:
  std::function<void()> on_destroy_;
};

TEST(DispatcherImplTest, DeferredDelete) {
  InSequence s;
  DispatcherImpl dispatcher;
  ReadyWatcher watcher1;

  dispatcher.deferredDelete(
      DeferredDeletablePtr{new TestDeferredDeletable([&]() -> void { watcher1.ready(); })});

  // The first one will get deleted inline.
  EXPECT_CALL(watcher1, ready());
  dispatcher.clearDeferredDeleteList();

  // This one does a nested deferred delete. We should need two clear calls to actually get
  // rid of it with the vector swapping. We also test that inline clear() call does nothing.
  ReadyWatcher watcher2;
  ReadyWatcher watcher3;
  dispatcher.deferredDelete(DeferredDeletablePtr{new TestDeferredDeletable([&]() -> void {
    watcher2.ready();
    dispatcher.deferredDelete(
        DeferredDeletablePtr{new TestDeferredDeletable([&]() -> void { watcher3.ready(); })});
    dispatcher.clearDeferredDeleteList();
  })});

  EXPECT_CALL(watcher2, ready());
  dispatcher.clearDeferredDeleteList();

  EXPECT_CALL(watcher3, ready());
  dispatcher.clearDeferredDeleteList();
}

// Test that the basic fork()/exec() works, and that PATH is searched for
// the binary
TEST(DispatcherImplTest, runProcess) {
  DispatcherImpl dispatcher;
  ChildProcessPtr child;
  TimerPtr timer;
  bool run_succeeded = false;
  dispatcher.post([&]() {
    child = dispatcher.runProcess({"true"}, [&](bool zero_exit_code) {
      run_succeeded = zero_exit_code;
      dispatcher.exit();
    });
  });
  dispatcher.run(Dispatcher::RunType::Block);
  EXPECT_TRUE(run_succeeded);

  run_succeeded = false;
  dispatcher.post([&]() {
    child = dispatcher.runProcess({"false"}, [&](bool zero_exit_code) {
      run_succeeded = !zero_exit_code;
      dispatcher.exit();
    });
  });
  dispatcher.run(Dispatcher::RunType::Block);
  EXPECT_TRUE(run_succeeded);

  run_succeeded = false;
  dispatcher.post([&]() {
    child = dispatcher.runProcess({"sh", "-c", "while true; do sleep 1; done"}, [&](bool) {
      // Infinite loop, so we should never get here
      run_succeeded = false;
      dispatcher.exit();
    });

    timer = dispatcher.createTimer([&]() {
      child.reset();
      dispatcher.exit();
      run_succeeded = true;
    });
    timer->enableTimer(std::chrono::milliseconds(5));
  });
  dispatcher.run(Dispatcher::RunType::Block);
  EXPECT_TRUE(run_succeeded);
}

class DispatcherChildManagerTest : public testing::Test {
public:
  DispatcherChildManagerTest()
      : os_sys_calls_(new NiceMock<MockOsSysCalls>), child_manager_(OsSysCallsPtr(os_sys_calls_)) {}

  // Make fork return a value indicating this is the child process
  void setupChild() { EXPECT_CALL(*os_sys_calls_, fork()).WillOnce(Return(0)); }

  // Make fork return a value indicating this is the parent process.
  // Returns the child pid
  pid_t setupParent(pid_t pid) {
    EXPECT_CALL(*os_sys_calls_, fork()).WillOnce(Return(pid));
    return pid;
  }

  pid_t setupParent() { return setupParent(10); }

  NiceMock<MockOsSysCalls>* os_sys_calls_;
  ChildManager child_manager_;
};

TEST_F(DispatcherChildManagerTest, ForkParent) {
  const pid_t pid = setupParent();
  ChildProcessPtr child = child_manager_.run({"foo"}, [](bool) {});
  EXPECT_TRUE(child_manager_.watched(pid));
  EXPECT_EQ(1, child_manager_.numWatched());
}

TEST_F(DispatcherChildManagerTest, ForkChild) {
  InSequence s;
  setupChild();
  EXPECT_CALL(*os_sys_calls_, open(StrEq("/dev/null"), O_RDWR)).WillOnce(Return(10));
  EXPECT_CALL(*os_sys_calls_, dup2(10, STDIN_FILENO));
  EXPECT_CALL(*os_sys_calls_, dup2(10, STDOUT_FILENO));
  EXPECT_CALL(*os_sys_calls_, dup2(10, STDERR_FILENO));
  EXPECT_CALL(*os_sys_calls_, execvp_(_, _));

  // Mock-execvp doesn't really execvp (obviously), so the call returns.
  // Any return from execvp is treated as a failure and results in a
  // call to _exit.
  EXPECT_CALL(*os_sys_calls_, _exit(Not(Eq(0))));

  child_manager_.run({"process_name"}, [](bool) {});
}

TEST_F(DispatcherChildManagerTest, ForkError) {
  EXPECT_CALL(*os_sys_calls_, fork()).WillOnce(Return(-1));
  EXPECT_THROW({ child_manager_.run({"process_name"}, [](bool) {}); }, EnvoyException);
}

TEST_F(DispatcherChildManagerTest, ExecMissingProcessName) {
  EXPECT_THROW({ child_manager_.run({}, [](bool) {}); }, EnvoyException);
}

TEST_F(DispatcherChildManagerTest, ExecEmptyProcessName) {
  EXPECT_THROW({ child_manager_.run({""}, [](bool) {}); }, EnvoyException);
}

TEST_F(DispatcherChildManagerTest, ExecArgsNone) {
  const char* process_name = "process_name";
  setupChild();
  EXPECT_CALL(*os_sys_calls_, execvp_(StrEq(process_name), ElementsAre(StrEq(process_name))));
  child_manager_.run({process_name}, [](bool) {});
}

TEST_F(DispatcherChildManagerTest, ExecArgsNoneProcessContainsSlash) {
  const char* process_name = "/a/path/to/process";
  setupChild();
  EXPECT_CALL(*os_sys_calls_, execvp_(StrEq(process_name), ElementsAre(StrEq(process_name))));
  child_manager_.run({process_name}, [](bool) {});
}

TEST_F(DispatcherChildManagerTest, ExecArgsMultiple) {
  const char* process_name = "process_name";
  const std::vector<std::string> args = {process_name, "arg1", "arg2", "arg3"};
  setupChild();
  EXPECT_CALL(*os_sys_calls_,
              execvp_(StrEq(process_name), ElementsAre(StrEq(process_name), StrEq("arg1"),
                                                       StrEq("arg2"), StrEq("arg3"))));
  child_manager_.run(args, [](bool) {});
}

struct ProcessCompletionCase {
  OsSysCalls::WaitpidStatus waitpid_status;
  bool expect_zero_exit;
  bool expect_callback_called;
};

class DispatcherChildManagerProcessCompletionTest
    : public DispatcherChildManagerTest,
      public testing::WithParamInterface<ProcessCompletionCase> {};

TEST_P(DispatcherChildManagerProcessCompletionTest, ProcessCompletionStatus) {
  const pid_t pid = setupParent();

  bool callback_run = false;
  bool zero_exit_code = false;
  ChildProcessPtr child = child_manager_.run({"sh"}, [&](bool zero_exit_code_param) {
    callback_run = true;
    zero_exit_code = zero_exit_code_param;
  });
  EXPECT_CALL(*os_sys_calls_, waitpid(-1, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(GetParam().waitpid_status), Return(pid)))
      .WillOnce(Return(-1));
  child_manager_.onSigChld();
  EXPECT_EQ(GetParam().expect_callback_called, callback_run);
  EXPECT_EQ(GetParam().expect_zero_exit, zero_exit_code);
}

static const ProcessCompletionCase processCompletionCases[] = {
    {{false, true, 0}, true, true},    // exited with 0
    {{false, true, 1}, false, true},   // exited non-zero
    {{false, true, -1}, false, true},  // exited negative non-zero
    {{true, false, 0}, false, true},   // signalled
    {{false, false, 0}, false, false}, // process didn't complete
};

INSTANTIATE_TEST_CASE_P(Cases, DispatcherChildManagerProcessCompletionTest,
                        testing::ValuesIn(processCompletionCases));

TEST_F(DispatcherChildManagerTest, WaitpidMultiple) {
  OsSysCalls::WaitpidStatus status1;
  status1.exited_ = true;
  status1.exit_status_ = 0;
  status1.signalled_ = false;

  OsSysCalls::WaitpidStatus status2;
  status2.exited_ = true;
  status2.exit_status_ = 1;
  status2.signalled_ = false;

  bool callback_run1 = false;
  bool callback_run2 = false;
  bool zero1 = false;
  bool zero2 = false;

  const pid_t pid1 = setupParent();
  ChildProcessPtr child1 = child_manager_.run({"sh"}, [&](bool zero_exit) {
    callback_run1 = true;
    zero1 = zero_exit;
  });

  const pid_t pid2 = setupParent(pid1 + 1);
  ChildProcessPtr child2 = child_manager_.run({"sh"}, [&](bool zero_exit) {
    callback_run2 = true;
    zero2 = zero_exit;
  });

  EXPECT_CALL(*os_sys_calls_, waitpid(-1, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(status1), Return(pid1)))
      .WillOnce(DoAll(SetArgReferee<1>(status2), Return(pid2)))
      .WillOnce(Return(-1));

  child_manager_.onSigChld();
  EXPECT_TRUE(callback_run1);
  EXPECT_TRUE(callback_run2);
  EXPECT_TRUE(zero1);
  EXPECT_FALSE(zero2);
}

TEST_F(DispatcherChildManagerTest, UnwatchedSigChld) {
  const pid_t pid = setupParent();
  OsSysCalls::WaitpidStatus status;
  status.exited_ = true;
  status.exit_status_ = 0;
  status.signalled_ = false;

  bool callback_run = false;
  ChildProcessPtr child = child_manager_.run({"sh"}, [&](bool) { callback_run = true; });
  EXPECT_CALL(*os_sys_calls_, waitpid(-1, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(status), Return(pid + 1)))
      .WillOnce(Return(-1));
  EXPECT_EQ(1, child_manager_.numWatched());
  child_manager_.onSigChld();
  EXPECT_FALSE(callback_run);
  EXPECT_EQ(1, child_manager_.numWatched());
}

TEST_F(DispatcherChildManagerTest, Cancellation) {
  const pid_t pid = setupParent();
  OsSysCalls::WaitpidStatus status;
  status.exited_ = true;
  status.exit_status_ = 0;
  status.signalled_ = false;

  bool callback_run = false;
  ChildProcessPtr child = child_manager_.run({"sh"}, [&](bool) { callback_run = true; });

  EXPECT_CALL(*os_sys_calls_, kill(pid, SIGTERM));
  EXPECT_TRUE(child_manager_.watched(pid));
  child.reset();
  EXPECT_FALSE(child_manager_.watched(pid));

  EXPECT_CALL(*os_sys_calls_, waitpid(-1, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(status), Return(pid)))
      .WillOnce(Return(-1));
  child_manager_.onSigChld();
  EXPECT_FALSE(callback_run);
}

TEST_F(DispatcherChildManagerTest, StaleCancellation) {
  const pid_t pid = setupParent();
  OsSysCalls::WaitpidStatus status;
  status.exited_ = true;
  status.exit_status_ = 0;
  status.signalled_ = false;

  ChildProcessPtr child1 = child_manager_.run({"sh"}, [&](bool) {});
  EXPECT_CALL(*os_sys_calls_, waitpid(-1, _, _))
      .WillOnce(DoAll(SetArgReferee<1>(status), Return(pid)))
      .WillOnce(Return(-1));
  EXPECT_TRUE(child_manager_.watched(pid));
  child_manager_.onSigChld();
  EXPECT_FALSE(child_manager_.watched(pid));

  const pid_t pid2 = setupParent(pid);
  ASSERT_EQ(pid, pid2); // Test doesn't work if these don't match
  ChildProcessPtr child2 = child_manager_.run({"sh"}, [&](bool) {});

  EXPECT_CALL(*os_sys_calls_, kill(_, _)).Times(0);
  child1.reset();

  EXPECT_CALL(*os_sys_calls_, kill(_, _)).Times(1);
  child2.reset();
}

} // namespace Event
} // namespace Envoy
