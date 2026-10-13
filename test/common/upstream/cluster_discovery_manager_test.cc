#include <functional>
#include <list>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/cleanup.h"
#include "source/common/upstream/cluster_discovery_manager.h"

#include "test/mocks/upstream/thread_local_cluster.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
namespace {

class TestClusterUpdateCallbacksHandle : public ClusterUpdateCallbacksHandle,
                                         RaiiListElement<ClusterUpdateCallbacks*> {
public:
  TestClusterUpdateCallbacksHandle(ClusterUpdateCallbacks& cb,
                                   std::list<ClusterUpdateCallbacks*>& parent)
      : RaiiListElement<ClusterUpdateCallbacks*>(parent, &cb) {}
};

class TestClusterLifecycleCallbackHandler : public ClusterLifecycleCallbackHandler {
public:
  // Upstream::ClusterLifecycleCallbackHandler
  ClusterUpdateCallbacksHandlePtr addClusterUpdateCallbacks(ClusterUpdateCallbacks& cb) override {
    return std::make_unique<TestClusterUpdateCallbacksHandle>(cb, update_callbacks_);
  }

  void invokeClusterAdded(ThreadLocalCluster& cluster) {
    for (auto& cb : update_callbacks_) {
      ThreadLocalClusterCommand command = [&cluster]() -> ThreadLocalCluster& { return cluster; };
      cb->onClusterAddOrUpdate(cluster.info()->name(), command);
    }
  }

  std::list<ClusterUpdateCallbacks*> update_callbacks_;
};

enum class Action {
  InvokePrevious,
  InvokeSelf,
  InvokeNext,
  InvokeLast,
  InvokeOther,
  ProcessFoo,
  ProcessBar,
  DestroyPrevious,
  DestroySelf,
  DestroyNext,
  DestroyOther,
  AddNewToFoo,
};

const char* actionToString(Action action) {
  switch (action) {
  case Action::InvokePrevious:
    return "invoke previous";

  case Action::InvokeSelf:
    return "invoke self";

  case Action::InvokeNext:
    return "invoke next";

  case Action::InvokeLast:
    return "invoke last";

  case Action::InvokeOther:
    return "invoke other";

  case Action::ProcessFoo:
    return "process foo";

  case Action::ProcessBar:
    return "process bar";

  case Action::DestroyPrevious:
    return "destroy previous";

  case Action::DestroySelf:
    return "destroy self";

  case Action::DestroyNext:
    return "destroy next";

  case Action::DestroyOther:
    return "destroy other";

  case Action::AddNewToFoo:
    return "add new to foo";
  }

  return "invalid action";
}

std::ostream& operator<<(std::ostream& os, Action action) {
  os << actionToString(action);
  return os;
}

enum class OtherActionsExecution {
  AfterFirstAction,
  WithinFirstAction,
};

struct ActionsParameter {
  ActionsParameter(std::vector<Action> actions, std::vector<std::string> called_callbacks,
                   OtherActionsExecution other_actions_execution)
      : actions_(std::move(actions)), called_callbacks_(std::move(called_callbacks)),
        other_actions_execution_(other_actions_execution) {}

  std::vector<Action> actions_;
  std::vector<std::string> called_callbacks_;
  OtherActionsExecution other_actions_execution_;
};

std::ostream& operator<<(std::ostream& os, const ActionsParameter& param) {
  const char* prefix = "";
  const char* first_separator = ", ";
  if (param.other_actions_execution_ == OtherActionsExecution::WithinFirstAction) {
    prefix = "during ";
    first_separator = ": ";
  }
  os << prefix << param.actions_.front() << first_separator
     << absl::StrJoin(param.actions_.begin() + 1, param.actions_.end(), ", ",
                      absl::StreamFormatter())
     << " => " << absl::StrJoin(param.called_callbacks_, ", ");
  return os;
}

class ActionExecutor {
public:
  ActionExecutor()
      : cdm_("test_thread", lifecycle_handler_), previous_(addCallback("foo", "previous")),
        self_(addCallback("foo", "self")), next_(addCallback("foo", "next")),
        last_(addCallback("foo", "last")), other_(addCallback("bar", "other")) {}

  void setSelfCallback(std::function<void()> self_callback) {
    self_callback_ = std::move(self_callback);
  }

  void execute(Action action) {
    switch (action) {
    case Action::InvokePrevious:
      useInvoker(previous_.invoker_);
      break;

    case Action::InvokeSelf:
      useInvoker(self_.invoker_);
      break;

    case Action::InvokeNext:
      useInvoker(next_.invoker_);
      break;

    case Action::InvokeLast:
      useInvoker(last_.invoker_);
      break;

    case Action::InvokeOther:
      useInvoker(other_.invoker_);
      break;

    case Action::ProcessFoo:
      processClusterName("foo");
      break;

    case Action::ProcessBar:
      processClusterName("bar");
      break;

    case Action::DestroyPrevious:
      previous_.handle_ptr_.reset();
      break;

    case Action::DestroySelf:
      self_.handle_ptr_.reset();
      break;

    case Action::DestroyNext:
      next_.handle_ptr_.reset();
      break;

    case Action::DestroyOther:
      other_.handle_ptr_.reset();
      break;

    case Action::AddNewToFoo:
      std::string callback_name = "new" + std::to_string(new_.size());
      new_.emplace_back(addCallback("foo", std::move(callback_name)));
      break;
    }
  }

  ClusterDiscoveryManager::AddedCallbackData addCallback(std::string cluster_name,
                                                         std::string callback_name) {
    return cdm_.addCallback(
        std::move(cluster_name),
        std::make_unique<ClusterDiscoveryCallback>(
            [this, callback_name = std::move(callback_name)](ClusterDiscoveryStatus) {
              // we ignore the status, it's a thing that always comes from outside the manager
              bool is_self = callback_name == "self";
              called_callbacks_.push_back(std::move(callback_name));
              if (is_self && self_callback_) {
                self_callback_();
              }
            }));
  }

  void processClusterName(std::string name) {
    auto cluster = NiceMock<MockThreadLocalCluster>();
    cluster.cluster_.info_->name_ = std::move(name);
    lifecycle_handler_.invokeClusterAdded(cluster);
  }

  void useInvoker(ClusterDiscoveryManager::CallbackInvoker& invoker) {
    invoker.invokeCallback(ClusterDiscoveryStatus::Available);
  }

  TestClusterLifecycleCallbackHandler lifecycle_handler_;
  ClusterDiscoveryManager cdm_;
  std::vector<std::string> called_callbacks_;
  ClusterDiscoveryManager::AddedCallbackData previous_, self_, next_, last_, other_;
  std::vector<ClusterDiscoveryManager::AddedCallbackData> new_;
  std::function<void()> self_callback_;
};

class ActionExecutorTest : public testing::TestWithParam<ActionsParameter> {
public:
  void runTest() {
    auto& [actions, expected_result, other_actions_execution] = GetParam();

    ASSERT_FALSE(actions.empty());

    switch (other_actions_execution) {
    case OtherActionsExecution::AfterFirstAction:
      for (auto action : actions) {
        executor_.execute(action);
      }
      break;

    case OtherActionsExecution::WithinFirstAction:
      executor_.setSelfCallback([this, begin = actions.begin() + 1, end = actions.end()]() {
        for (auto it = begin; it != end; ++it) {
          executor_.execute(*it);
        }
      });
      executor_.execute(actions.front());
      break;
    }

    EXPECT_EQ(executor_.called_callbacks_, expected_result);
  }

  ActionExecutor executor_;
};

std::vector<ActionsParameter> all_actions = {
    // invoke self twice in a row; expect it to be called once
    ActionsParameter({Action::InvokeSelf, Action::InvokeSelf}, {"self"},
                     OtherActionsExecution::AfterFirstAction),
    // invoke self then other; expect them to be called normally
    ActionsParameter({Action::InvokeSelf, Action::InvokeOther}, {"self", "other"},
                     OtherActionsExecution::AfterFirstAction),
    // invoke self then process foo; since self was already called, processing foo should not call
    // it again
    ActionsParameter({Action::InvokeSelf, Action::ProcessFoo}, {"self", "previous", "next", "last"},
                     OtherActionsExecution::AfterFirstAction),
    // invoke self then process bar; expect them to be called normally
    ActionsParameter({Action::InvokeSelf, Action::ProcessBar}, {"self", "other"},
                     OtherActionsExecution::AfterFirstAction),
    // invoke self then destroy self; expect destroying to be a noop instead of corrupting things
    // (this is mostly for address sanitizer)
    ActionsParameter({Action::InvokeSelf, Action::DestroySelf}, {"self"},
                     OtherActionsExecution::AfterFirstAction),
    // process foo then invoke self; since self was called as a part of processing foo, invoke
    // should be a noop
    ActionsParameter({Action::ProcessFoo, Action::InvokeSelf}, {"previous", "self", "next", "last"},
                     OtherActionsExecution::AfterFirstAction),
    // process foo twice; expect the callbacks to be called once
    ActionsParameter({Action::ProcessFoo, Action::ProcessFoo}, {"previous", "self", "next", "last"},
                     OtherActionsExecution::AfterFirstAction),
    // process foo then bar; expect the callbacks to be called normally
    ActionsParameter({Action::ProcessFoo, Action::ProcessBar},
                     {"previous", "self", "next", "last", "other"},
                     OtherActionsExecution::AfterFirstAction),
    // process foo then destroy self; expect destroying to be a noop instead of corrupting things
    // (this is mostly for address sanitizer)
    ActionsParameter({Action::ProcessFoo, Action::DestroySelf},
                     {"previous", "self", "next", "last"}, OtherActionsExecution::AfterFirstAction),
    // destroy self then invoke self; expect the invoke to be a noop
    ActionsParameter({Action::DestroySelf, Action::InvokeSelf}, {},
                     OtherActionsExecution::AfterFirstAction),
    // destroy self then process foo; expect all callbacks but self to be invoked
    ActionsParameter({Action::DestroySelf, Action::ProcessFoo}, {"previous", "next", "last"},
                     OtherActionsExecution::AfterFirstAction),
    // destroy self twice; expect the second destroying to be a noop instead of corrupting things
    // (this is mostly for address sanitizer)
    ActionsParameter({Action::DestroySelf, Action::DestroySelf}, {},
                     OtherActionsExecution::AfterFirstAction),

    // when invoking self, invoke self; expect the second invoke to be a noop
    ActionsParameter({Action::InvokeSelf, Action::InvokeSelf}, {"self"},
                     OtherActionsExecution::WithinFirstAction),
    // when invoking self, destroy self; expect the second destroying to be a noop instead of
    // corrupting things (this is mostly for address sanitizer)
    ActionsParameter({Action::InvokeSelf, Action::DestroySelf}, {"self"},
                     OtherActionsExecution::WithinFirstAction),
    // when invoking self, process foo; expect all callbacks but self to be invoked, since self was
    // already invoked
    ActionsParameter({Action::InvokeSelf, Action::ProcessFoo}, {"self", "previous", "next", "last"},
                     OtherActionsExecution::WithinFirstAction),
    // when invoking self, process bar; expect the callbacks to be called normally
    ActionsParameter({Action::InvokeSelf, Action::ProcessBar}, {"self", "other"},
                     OtherActionsExecution::WithinFirstAction),
    // when processing foo, invoke previous; expect the invoke to be a noop, because previous has
    // already been called and done
    ActionsParameter({Action::ProcessFoo, Action::InvokePrevious},
                     {"previous", "self", "next", "last"},
                     OtherActionsExecution::WithinFirstAction),
    // when processing foo, invoke self; expect the invoke to be a noop, because self is being
    // called right now
    ActionsParameter({Action::ProcessFoo, Action::InvokeSelf}, {"previous", "self", "next", "last"},
                     OtherActionsExecution::WithinFirstAction),
    // when processing foo, invoke next; expect next to be called once (with the invoke), while
    // calling it during the process should become a noop
    ActionsParameter({Action::ProcessFoo, Action::InvokeNext}, {"previous", "self", "next", "last"},
                     OtherActionsExecution::WithinFirstAction),
    // when processing foo, invoke last; expect last to be called out of order
    ActionsParameter({Action::ProcessFoo, Action::InvokeLast}, {"previous", "self", "last", "next"},
                     OtherActionsExecution::WithinFirstAction),
    // when processing foo, process foo; expect the second process to be a noop
    ActionsParameter({Action::ProcessFoo, Action::ProcessFoo}, {"previous", "self", "next", "last"},
                     OtherActionsExecution::WithinFirstAction),
    // when processing foo, process bar; expect the callbacks to be called normally, but bar
    // callbacks should be called before the rest of foo callbacks
    ActionsParameter({Action::ProcessFoo, Action::ProcessBar},
                     {"previous", "self", "other", "next", "last"},
                     OtherActionsExecution::WithinFirstAction),
    // when processing foo, destroy self; expect the second destroying to be a noop (since self is
    // being called at the moment), instead of corrupting things (this is mostly for address
    // sanitizer)
    ActionsParameter({Action::ProcessFoo, Action::DestroySelf},
                     {"previous", "self", "next", "last"},
                     OtherActionsExecution::WithinFirstAction),
    // when processing foo, destroy next; expect next callback to be skipped
    ActionsParameter({Action::ProcessFoo, Action::DestroyNext}, {"previous", "self", "last"},
                     OtherActionsExecution::WithinFirstAction),
    // when processing foo, add a new callback to foo; expect the new callback to be not invoked
    // (could be invoked with a follow-up process)
    ActionsParameter({Action::ProcessFoo, Action::AddNewToFoo},
                     {"previous", "self", "next", "last"},
                     OtherActionsExecution::WithinFirstAction),

    // when invoking self, invoke previous and process foo; expect the process to call only next and
    // last
    ActionsParameter({Action::InvokeSelf, Action::InvokePrevious, Action::ProcessFoo},
                     {"self", "previous", "next", "last"},
                     OtherActionsExecution::WithinFirstAction),
    // when invoking self, invoke next and process foo; expect process to call only previous and
    // last
    ActionsParameter({Action::InvokeSelf, Action::InvokeNext, Action::ProcessFoo},
                     {"self", "next", "previous", "last"},
                     OtherActionsExecution::WithinFirstAction),
    // when invoking self, invoke other invoke last, and process bar; expect the process to be a
    // noop (invoking last for visibility of the noop)
    ActionsParameter(
        {Action::InvokeSelf, Action::InvokeOther, Action::InvokeLast, Action::ProcessBar},
        {"self", "other", "last"}, OtherActionsExecution::WithinFirstAction),
    // when invoking self, process foo then invoke previous; expect the process to skip self (as
    // it's being called at the moment) and invoking previous to be a noop (called during the
    // process)
    ActionsParameter({Action::InvokeSelf, Action::ProcessFoo, Action::InvokePrevious},
                     {"self", "previous", "next", "last"},
                     OtherActionsExecution::WithinFirstAction),
    // when invoking self, process foo then invoke previous; expect the process to skip self (as
    // it's being called at the moment) and invoking previous to be a noop (called during the
    // process)
    ActionsParameter({Action::InvokeSelf, Action::ProcessFoo, Action::DestroyPrevious},
                     {"self", "previous", "next", "last"},
                     OtherActionsExecution::WithinFirstAction),
    // when invoking self, destroy previous and process foo; expect self and previous to be skipped
    // when processing
    ActionsParameter({Action::InvokeSelf, Action::DestroyPrevious, Action::ProcessFoo},
                     {"self", "next", "last"}, OtherActionsExecution::WithinFirstAction),
    // when invoking self, destroy other and process bar; expect the process to be a noop
    ActionsParameter({Action::InvokeSelf, Action::DestroyOther, Action::ProcessBar}, {"self"},
                     OtherActionsExecution::WithinFirstAction),
    // when invoking self, add new callback to foo and process foo; expect self to be skipped, but
    // new to be called along with the rest of the callbacks
    ActionsParameter({Action::InvokeSelf, Action::AddNewToFoo, Action::ProcessFoo},
                     {"self", "previous", "next", "last", "new0"},
                     OtherActionsExecution::WithinFirstAction),
    // when processing foo, add new callback to foo, process foo then invoke other; expect the
    // second process to call only the new callback, then first process to resume with the rest of
    // the callbacks (the other callback is added to see the split between two processes)
    ActionsParameter(
        {Action::ProcessFoo, Action::AddNewToFoo, Action::ProcessFoo, Action::InvokeOther},
        {"previous", "self", "new0", "other", "next", "last"},
        OtherActionsExecution::WithinFirstAction),
    // when processing foo, add new to foo and destroy next; expect the new callback to be not
    // called, same for the next callback
    ActionsParameter({Action::ProcessFoo, Action::AddNewToFoo, Action::DestroyNext},
                     {"previous", "self", "last"}, OtherActionsExecution::WithinFirstAction),
    // when processing foo, destroy next and try to invoke next;
    // expect the invoke to be noop, and processing to not call the
    // next callback
    ActionsParameter({Action::ProcessFoo, Action::DestroyNext, Action::InvokeNext},
                     {"previous", "self", "last"}, OtherActionsExecution::WithinFirstAction),
};

class ClusterDiscoveryTest : public ActionExecutorTest {};

INSTANTIATE_TEST_SUITE_P(ClusterDiscoveryTestActions, ClusterDiscoveryTest,
                         testing::ValuesIn(all_actions));

TEST_P(ClusterDiscoveryTest, TestActions) { runTest(); }

class ClusterDiscoveryManagerMiscTest : public testing::Test {
public:
  ClusterDiscoveryManagerMiscTest() = default;

  ActionExecutor executor_;
};

// Test the the discovery in progress value is correct.
TEST_F(ClusterDiscoveryManagerMiscTest, TestDiscoveryInProgressValue) {
  // previous is first callback added to foo
  EXPECT_FALSE(executor_.previous_.discovery_in_progress_);
  // self, next and last callbacks are follow-up callbacks in foo
  EXPECT_TRUE(executor_.self_.discovery_in_progress_);
  EXPECT_TRUE(executor_.next_.discovery_in_progress_);
  EXPECT_TRUE(executor_.last_.discovery_in_progress_);
  // other is first callback added to bar
  EXPECT_FALSE(executor_.other_.discovery_in_progress_);
}

} // namespace
} // namespace Upstream
} // namespace Envoy
