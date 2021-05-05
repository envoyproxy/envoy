#include <algorithm>
#include <list>
#include <iostream>
#include <ostream>
#include <vector>

#include "envoy/upstream/cluster_manager.h"

#include "common/common/cleanup.h"
#include "common/upstream/cluster_discovery_manager.h"

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
      cb->onClusterAddOrUpdate(cluster);
    }
  }

  std::list<ClusterUpdateCallbacks*> update_callbacks_;
};

enum class Action {
  InvokePrevious,
  InvokeSelf,
  InvokeNext,
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

/*
std::array all_simple_first_actions = {
    Action::InvokeSelf,
    Action::ProcessFoo,
    Action::DestroySelf,
};

std::array all_simple_other_actions = {Action::InvokeSelf,  Action::InvokeOther,
                                       Action::ProcessFoo,  Action::ProcessBar,
                                       Action::DestroySelf, Action::DestroyOther};

std::array all_complex_first_actions = {Action::InvokeSelf, Action::ProcessFoo};

std::array all_complex_other_actions = {
    Action::InvokePrevious, Action::InvokeSelf,   Action::InvokeNext,      Action::InvokeOther,
    Action::ProcessFoo,     Action::ProcessBar,   Action::DestroyPrevious, Action::DestroySelf,
    Action::DestroyNext,    Action::DestroyOther, Action::AddNewToFoo,
};
*/

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

class ActionsCombinator {
public:
  template <std::size_t FirstActionsSize, std::size_t OtherActionsSize>
  ActionsCombinator(std::array<Action, FirstActionsSize> const& first_actions,
                    std::array<Action, OtherActionsSize> const& other_actions,
                    unsigned other_actions_count)
      : first_actions_(first_actions.begin(), first_actions.end()),
        other_actions_(other_actions.begin(), other_actions.end()),
        current_state_(1 + other_actions_count) {}

  bool next() {
    if (indices_.has_value()) {
      auto& indices = *indices_;
      for (auto idx = current_state_.size() - 1; idx > 0; --idx) {
        if (indices[idx] != other_actions_.size() - 1) {
          ++indices[idx];
          return true;
        } else {
          indices[idx] = 0;
        }
      }
      if (indices[0] != first_actions_.size() - 1) {
        ++indices[0];
        return true;
      }
      indices_.reset();
      return false;
    }
    std::vector<unsigned> new_indices;
    new_indices.assign(current_state_.size(), 0);
    indices_ = std::move(new_indices);
    return true;
  }

  std::vector<Action>& state() {
    auto& indices = *indices_;
    current_state_[0] = first_actions_[indices[0]];
    for (auto idx{1u}; idx < current_state_.size(); ++idx) {
      current_state_[idx] = other_actions_[indices[idx]];
    }
    return current_state_;
  }

private:
  const std::vector<Action> first_actions_;
  const std::vector<Action> other_actions_;
  std::vector<Action> current_state_;
  absl::optional<std::vector<unsigned>> indices_;
};

class ActionsCombinatorTest : public testing::Test {};

TEST_F(ActionsCombinatorTest, TestCombinator) {
  std::array first_actions = {Action::InvokeSelf, Action::ProcessFoo};
  std::array other_actions = {Action::InvokeSelf, Action::ProcessFoo, Action::DestroySelf};

  ActionsCombinator combinator(first_actions, other_actions, 2);
  std::vector<std::vector<Action>> expected_actions = {
      {Action::InvokeSelf, Action::InvokeSelf, Action::InvokeSelf},
      {Action::InvokeSelf, Action::InvokeSelf, Action::ProcessFoo},
      {Action::InvokeSelf, Action::InvokeSelf, Action::DestroySelf},
      {Action::InvokeSelf, Action::ProcessFoo, Action::InvokeSelf},
      {Action::InvokeSelf, Action::ProcessFoo, Action::ProcessFoo},
      {Action::InvokeSelf, Action::ProcessFoo, Action::DestroySelf},
      {Action::InvokeSelf, Action::DestroySelf, Action::InvokeSelf},
      {Action::InvokeSelf, Action::DestroySelf, Action::ProcessFoo},
      {Action::InvokeSelf, Action::DestroySelf, Action::DestroySelf},
      {Action::ProcessFoo, Action::InvokeSelf, Action::InvokeSelf},
      {Action::ProcessFoo, Action::InvokeSelf, Action::ProcessFoo},
      {Action::ProcessFoo, Action::InvokeSelf, Action::DestroySelf},
      {Action::ProcessFoo, Action::ProcessFoo, Action::InvokeSelf},
      {Action::ProcessFoo, Action::ProcessFoo, Action::ProcessFoo},
      {Action::ProcessFoo, Action::ProcessFoo, Action::DestroySelf},
      {Action::ProcessFoo, Action::DestroySelf, Action::InvokeSelf},
      {Action::ProcessFoo, Action::DestroySelf, Action::ProcessFoo},
      {Action::ProcessFoo, Action::DestroySelf, Action::DestroySelf},
  };

  std::vector<std::vector<Action>> actions;
  actions.reserve(expected_actions.size());
  while (combinator.next()) {
    actions.push_back(combinator.state());
    EXPECT_LE(actions.size(), expected_actions.size());
    if (actions.size() > expected_actions.size()) {
      // we failed anyway
      break;
    }
  }
  EXPECT_EQ(actions.size(), expected_actions.size());
  EXPECT_EQ(actions, expected_actions);
}

/*
class CxxActionsParameter {
public:
  CxxActionsParameter(const ActionsParameter& parameter)
    : parameter_(parameter)
  {}

  const ActionsParameter& parameter_;
};

const char* actionToCxxString(Action action) {
  switch (action) {
  case Action::InvokePrevious:
    return "Action::InvokePrevious";

  case Action::InvokeSelf:
    return "Action::InvokeSelf";

  case Action::InvokeNext:
    return "Action::InvokeNext";

  case Action::InvokeOther:
    return "Action::InvokeOther";

  case Action::ProcessFoo:
    return "Action::ProcessFoo";

  case Action::ProcessBar:
    return "Action::ProcessBar";

  case Action::DestroyPrevious:
    return "Action::DestroyPrevious";

  case Action::DestroySelf:
    return "Action::DestroySelf";

  case Action::DestroyNext:
    return "Action::DestroyNext";

  case Action::DestroyOther:
    return "Action::DestroyOther";

  case Action::AddNewToFoo:
    return "Action::AddNewToFoo";
  }

  return "invalid_action_fail_to_compile";
}

const char* other_actions_execution_to_string(OtherActionsExecution execution) {
  switch (execution) {
  case OtherActionsExecution::AfterFirstAction:
    return "OtherActionsExecution::AfterFirstAction";
  case OtherActionsExecution::WithinFirstAction:
    return "OtherActionsExecution::WithinFirstAction";
  }

  return "invalid_other_action_execution_fail_to_compile";
}

std::ostream& operator<<(std::ostream& os, const CxxActionsParameter& parameter) {
  const ActionsParameter& param = parameter.parameter_;
  os << "ActionsParameter({" << absl::StrJoin(param.actions_, ", ", [](std::string* out, Action action) { out->append(actionToCxxString(action)); }) << "}, {" << absl::StrJoin(param.called_callbacks_, ", ", [](std::string* out, std::string name) {out->append(1, '"'); out->append(name); out->append(1, '"');}) << "}, " << other_actions_execution_to_string(param.other_actions_execution_) << ")";
  return os;
}
*/

std::vector<ActionsParameter>
generateActionsParameters(ActionsCombinator combinator,
                          OtherActionsExecution other_actions_execution) {
  std::vector<ActionsParameter> parameters;

  while (combinator.next()) {
    auto& actions = combinator.state();
    // callbacks available for processing cluster foo
    std::vector<std::string> available = {"previous", "self", "next"};
    std::vector<std::string> called_callbacks;
    // callbacks called or destroyed
    std::vector<std::string> used;
    // callbacks scheduled for processing, but not yet called
    std::vector<std::string> scheduled;
    unsigned new_counter = 0;

    auto contains =
        [](std::vector<std::string>& v,
           absl::string_view name) -> absl::optional<std::vector<std::string>::iterator> {
      if (auto it = std::find(v.begin(), v.end(), name); it != v.end()) {
        return {it};
      }
      return {};
    };
    auto erase = [&contains](std::vector<std::string>& v, absl::string_view name) {
      if (auto opt_it = contains(v, name); opt_it.has_value()) {
        v.erase(*opt_it);
      }
    };
    auto schedule = [&scheduled, &erase, &available](absl::string_view name) mutable {
      scheduled.emplace_back(name);
      erase(available, name);
    };
    auto use = [&used, &erase, &scheduled, &available](absl::string_view name) mutable {
      used.emplace_back(name);
      erase(scheduled, name);
      erase(available, name);
    };
    auto call = [&contains, &used, &called_callbacks, &use](absl::string_view name) mutable {
      if (!contains(used, name)) {
        called_callbacks.emplace_back(name);
        use(name);
      }
    };

    auto first_action = actions.front();
    switch (first_action) {
    case Action::InvokeSelf:
      call("self");
      break;

    case Action::ProcessFoo:
      call("previous");
      call("self");
      switch (other_actions_execution) {
      case OtherActionsExecution::AfterFirstAction:
        call("next");
        break;
      case OtherActionsExecution::WithinFirstAction:
        schedule("next");
        break;
      }
      // not doing anything with "new" - it's added in the
      // AddNewToFoo action, which is never a first action.
      break;

    case Action::DestroySelf:
      use("self");
      break;

    default:
      ADD_FAILURE() << first_action << " should not be a first action";
      break;
    }

    for (auto idx = 1u; idx < actions.size(); ++idx) {
      switch (actions[idx]) {
      case Action::InvokePrevious:
        call("previous");
        break;

      case Action::InvokeSelf:
        call("self");
        break;

      case Action::InvokeNext:
        call("next");
        break;

      case Action::InvokeOther:
        call("other");
        break;

      case Action::ProcessFoo: {
        // call modifies available during iteration, so make a copy
        // and iterate it
        auto copy = available;
        for (const auto& n : copy) {
          call(n);
        }
        break;
      }

      case Action::ProcessBar:
        call("other");
        break;

      case Action::DestroyPrevious:
        use("previous");
        break;

      case Action::DestroySelf:
        use("self");
        break;

      case Action::DestroyNext:
        use("next");
        break;

      case Action::DestroyOther:
        use("other");
        break;

      case Action::AddNewToFoo:
        available.emplace_back("new" + std::to_string(new_counter));
        ++new_counter;
        break;
      }
    }

    auto scheduled_copy = scheduled;
    for (const auto& name : scheduled_copy) {
      call(name);
    }

    parameters.emplace_back(actions, std::move(called_callbacks), other_actions_execution);
  }

  /*
  std::cout << "std::vector<ActionsParameter> all_actions {\n";
  for (auto const& parameter : parameters) {
    std::cout << CxxActionsParameter(parameter) << ",\n";
  }
  std::cout << "}\n";
  ADD_FAILURE() << "failing for fun";
  */

  return parameters;
}

class ActionExecutor {
public:
  ActionExecutor()
      : lifecycle_handler_(), cdm_("test_thread", lifecycle_handler_),
        previous_(addCallback("foo", "previous")), self_(addCallback("foo", "self")),
        next_(addCallback("foo", "next")), other_(addCallback("bar", "other")) {}

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
    auto cluster = MockThreadLocalCluster();
    cluster.cluster_.info_->name_ = std::move(name);
    // Silence warnings about "Uninteresting mock function call".
    EXPECT_CALL(cluster, info());
    lifecycle_handler_.invokeClusterAdded(cluster);
  }

  void useInvoker(ClusterDiscoveryManager::CallbackInvoker& invoker) {
    invoker.invokeCallback(ClusterDiscoveryStatus::Available);
  }

  std::vector<std::string>& calledCallbacks() { return called_callbacks_; }

private:
  TestClusterLifecycleCallbackHandler lifecycle_handler_;
  ClusterDiscoveryManager cdm_;
  std::vector<std::string> called_callbacks_;
  ClusterDiscoveryManager::AddedCallbackData previous_, self_, next_, other_;
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

    EXPECT_EQ(executor_.calledCallbacks(), expected_result);
  }

private:
  ActionExecutor executor_;
};

std::vector<ActionsParameter> all_actions = {
  ActionsParameter({Action::InvokeSelf, Action::InvokeSelf}, {"self"}, OtherActionsExecution::AfterFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeOther}, {"self", "other"}, OtherActionsExecution::AfterFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::ProcessFoo}, {"self", "previous", "next"}, OtherActionsExecution::AfterFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::ProcessBar}, {"self", "other"}, OtherActionsExecution::AfterFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroySelf}, {"self"}, OtherActionsExecution::AfterFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyOther}, {"self"}, OtherActionsExecution::AfterFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeSelf}, {"previous", "self", "next"}, OtherActionsExecution::AfterFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeOther}, {"previous", "self", "next", "other"}, OtherActionsExecution::AfterFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::ProcessFoo}, {"previous", "self", "next"}, OtherActionsExecution::AfterFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::ProcessBar}, {"previous", "self", "next", "other"}, OtherActionsExecution::AfterFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroySelf}, {"previous", "self", "next"}, OtherActionsExecution::AfterFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyOther}, {"previous", "self", "next"}, OtherActionsExecution::AfterFirstAction),
  ActionsParameter({Action::DestroySelf, Action::InvokeSelf}, {}, OtherActionsExecution::AfterFirstAction),
  ActionsParameter({Action::DestroySelf, Action::InvokeOther}, {"other"}, OtherActionsExecution::AfterFirstAction),
  ActionsParameter({Action::DestroySelf, Action::ProcessFoo}, {"previous", "next"}, OtherActionsExecution::AfterFirstAction),
  ActionsParameter({Action::DestroySelf, Action::ProcessBar}, {"other"}, OtherActionsExecution::AfterFirstAction),
  ActionsParameter({Action::DestroySelf, Action::DestroySelf}, {}, OtherActionsExecution::AfterFirstAction),
  ActionsParameter({Action::DestroySelf, Action::DestroyOther}, {}, OtherActionsExecution::AfterFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokePrevious, Action::InvokePrevious}, {"self", "previous"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokePrevious, Action::InvokeSelf}, {"self", "previous"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokePrevious, Action::InvokeNext}, {"self", "previous", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokePrevious, Action::InvokeOther}, {"self", "previous", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokePrevious, Action::ProcessFoo}, {"self", "previous", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokePrevious, Action::ProcessBar}, {"self", "previous", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokePrevious, Action::DestroyPrevious}, {"self", "previous"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokePrevious, Action::DestroySelf}, {"self", "previous"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokePrevious, Action::DestroyNext}, {"self", "previous"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokePrevious, Action::DestroyOther}, {"self", "previous"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokePrevious, Action::AddNewToFoo}, {"self", "previous"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeSelf, Action::InvokePrevious}, {"self", "previous"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeSelf, Action::InvokeSelf}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeSelf, Action::InvokeNext}, {"self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeSelf, Action::InvokeOther}, {"self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeSelf, Action::ProcessFoo}, {"self", "previous", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeSelf, Action::ProcessBar}, {"self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeSelf, Action::DestroyPrevious}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeSelf, Action::DestroySelf}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeSelf, Action::DestroyNext}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeSelf, Action::DestroyOther}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeSelf, Action::AddNewToFoo}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeNext, Action::InvokePrevious}, {"self", "next", "previous"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeNext, Action::InvokeSelf}, {"self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeNext, Action::InvokeNext}, {"self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeNext, Action::InvokeOther}, {"self", "next", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeNext, Action::ProcessFoo}, {"self", "next", "previous"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeNext, Action::ProcessBar}, {"self", "next", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeNext, Action::DestroyPrevious}, {"self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeNext, Action::DestroySelf}, {"self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeNext, Action::DestroyNext}, {"self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeNext, Action::DestroyOther}, {"self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeNext, Action::AddNewToFoo}, {"self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeOther, Action::InvokePrevious}, {"self", "other", "previous"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeOther, Action::InvokeSelf}, {"self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeOther, Action::InvokeNext}, {"self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeOther, Action::InvokeOther}, {"self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeOther, Action::ProcessFoo}, {"self", "other", "previous", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeOther, Action::ProcessBar}, {"self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeOther, Action::DestroyPrevious}, {"self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeOther, Action::DestroySelf}, {"self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeOther, Action::DestroyNext}, {"self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeOther, Action::DestroyOther}, {"self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::InvokeOther, Action::AddNewToFoo}, {"self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::ProcessFoo, Action::InvokePrevious}, {"self", "previous", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::ProcessFoo, Action::InvokeSelf}, {"self", "previous", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::ProcessFoo, Action::InvokeNext}, {"self", "previous", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::ProcessFoo, Action::InvokeOther}, {"self", "previous", "next", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::ProcessFoo, Action::ProcessFoo}, {"self", "previous", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::ProcessFoo, Action::ProcessBar}, {"self", "previous", "next", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::ProcessFoo, Action::DestroyPrevious}, {"self", "previous", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::ProcessFoo, Action::DestroySelf}, {"self", "previous", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::ProcessFoo, Action::DestroyNext}, {"self", "previous", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::ProcessFoo, Action::DestroyOther}, {"self", "previous", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::ProcessFoo, Action::AddNewToFoo}, {"self", "previous", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::ProcessBar, Action::InvokePrevious}, {"self", "other", "previous"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::ProcessBar, Action::InvokeSelf}, {"self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::ProcessBar, Action::InvokeNext}, {"self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::ProcessBar, Action::InvokeOther}, {"self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::ProcessBar, Action::ProcessFoo}, {"self", "other", "previous", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::ProcessBar, Action::ProcessBar}, {"self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::ProcessBar, Action::DestroyPrevious}, {"self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::ProcessBar, Action::DestroySelf}, {"self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::ProcessBar, Action::DestroyNext}, {"self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::ProcessBar, Action::DestroyOther}, {"self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::ProcessBar, Action::AddNewToFoo}, {"self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyPrevious, Action::InvokePrevious}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyPrevious, Action::InvokeSelf}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyPrevious, Action::InvokeNext}, {"self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyPrevious, Action::InvokeOther}, {"self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyPrevious, Action::ProcessFoo}, {"self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyPrevious, Action::ProcessBar}, {"self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyPrevious, Action::DestroyPrevious}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyPrevious, Action::DestroySelf}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyPrevious, Action::DestroyNext}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyPrevious, Action::DestroyOther}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyPrevious, Action::AddNewToFoo}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroySelf, Action::InvokePrevious}, {"self", "previous"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroySelf, Action::InvokeSelf}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroySelf, Action::InvokeNext}, {"self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroySelf, Action::InvokeOther}, {"self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroySelf, Action::ProcessFoo}, {"self", "previous", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroySelf, Action::ProcessBar}, {"self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroySelf, Action::DestroyPrevious}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroySelf, Action::DestroySelf}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroySelf, Action::DestroyNext}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroySelf, Action::DestroyOther}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroySelf, Action::AddNewToFoo}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyNext, Action::InvokePrevious}, {"self", "previous"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyNext, Action::InvokeSelf}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyNext, Action::InvokeNext}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyNext, Action::InvokeOther}, {"self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyNext, Action::ProcessFoo}, {"self", "previous"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyNext, Action::ProcessBar}, {"self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyNext, Action::DestroyPrevious}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyNext, Action::DestroySelf}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyNext, Action::DestroyNext}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyNext, Action::DestroyOther}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyNext, Action::AddNewToFoo}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyOther, Action::InvokePrevious}, {"self", "previous"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyOther, Action::InvokeSelf}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyOther, Action::InvokeNext}, {"self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyOther, Action::InvokeOther}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyOther, Action::ProcessFoo}, {"self", "previous", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyOther, Action::ProcessBar}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyOther, Action::DestroyPrevious}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyOther, Action::DestroySelf}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyOther, Action::DestroyNext}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyOther, Action::DestroyOther}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::DestroyOther, Action::AddNewToFoo}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::AddNewToFoo, Action::InvokePrevious}, {"self", "previous"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::AddNewToFoo, Action::InvokeSelf}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::AddNewToFoo, Action::InvokeNext}, {"self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::AddNewToFoo, Action::InvokeOther}, {"self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::AddNewToFoo, Action::ProcessFoo}, {"self", "previous", "next", "new0"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::AddNewToFoo, Action::ProcessBar}, {"self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::AddNewToFoo, Action::DestroyPrevious}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::AddNewToFoo, Action::DestroySelf}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::AddNewToFoo, Action::DestroyNext}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::AddNewToFoo, Action::DestroyOther}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::InvokeSelf, Action::AddNewToFoo, Action::AddNewToFoo}, {"self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokePrevious, Action::InvokePrevious}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokePrevious, Action::InvokeSelf}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokePrevious, Action::InvokeNext}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokePrevious, Action::InvokeOther}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokePrevious, Action::ProcessFoo}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokePrevious, Action::ProcessBar}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokePrevious, Action::DestroyPrevious}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokePrevious, Action::DestroySelf}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokePrevious, Action::DestroyNext}, {"previous", "self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokePrevious, Action::DestroyOther}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokePrevious, Action::AddNewToFoo}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeSelf, Action::InvokePrevious}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeSelf, Action::InvokeSelf}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeSelf, Action::InvokeNext}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeSelf, Action::InvokeOther}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeSelf, Action::ProcessFoo}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeSelf, Action::ProcessBar}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeSelf, Action::DestroyPrevious}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeSelf, Action::DestroySelf}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeSelf, Action::DestroyNext}, {"previous", "self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeSelf, Action::DestroyOther}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeSelf, Action::AddNewToFoo}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeNext, Action::InvokePrevious}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeNext, Action::InvokeSelf}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeNext, Action::InvokeNext}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeNext, Action::InvokeOther}, {"previous", "self", "next", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeNext, Action::ProcessFoo}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeNext, Action::ProcessBar}, {"previous", "self", "next", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeNext, Action::DestroyPrevious}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeNext, Action::DestroySelf}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeNext, Action::DestroyNext}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeNext, Action::DestroyOther}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeNext, Action::AddNewToFoo}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeOther, Action::InvokePrevious}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeOther, Action::InvokeSelf}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeOther, Action::InvokeNext}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeOther, Action::InvokeOther}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeOther, Action::ProcessFoo}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeOther, Action::ProcessBar}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeOther, Action::DestroyPrevious}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeOther, Action::DestroySelf}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeOther, Action::DestroyNext}, {"previous", "self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeOther, Action::DestroyOther}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::InvokeOther, Action::AddNewToFoo}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::ProcessFoo, Action::InvokePrevious}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::ProcessFoo, Action::InvokeSelf}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::ProcessFoo, Action::InvokeNext}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::ProcessFoo, Action::InvokeOther}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::ProcessFoo, Action::ProcessFoo}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::ProcessFoo, Action::ProcessBar}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::ProcessFoo, Action::DestroyPrevious}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::ProcessFoo, Action::DestroySelf}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::ProcessFoo, Action::DestroyNext}, {"previous", "self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::ProcessFoo, Action::DestroyOther}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::ProcessFoo, Action::AddNewToFoo}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::ProcessBar, Action::InvokePrevious}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::ProcessBar, Action::InvokeSelf}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::ProcessBar, Action::InvokeNext}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::ProcessBar, Action::InvokeOther}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::ProcessBar, Action::ProcessFoo}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::ProcessBar, Action::ProcessBar}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::ProcessBar, Action::DestroyPrevious}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::ProcessBar, Action::DestroySelf}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::ProcessBar, Action::DestroyNext}, {"previous", "self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::ProcessBar, Action::DestroyOther}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::ProcessBar, Action::AddNewToFoo}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyPrevious, Action::InvokePrevious}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyPrevious, Action::InvokeSelf}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyPrevious, Action::InvokeNext}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyPrevious, Action::InvokeOther}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyPrevious, Action::ProcessFoo}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyPrevious, Action::ProcessBar}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyPrevious, Action::DestroyPrevious}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyPrevious, Action::DestroySelf}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyPrevious, Action::DestroyNext}, {"previous", "self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyPrevious, Action::DestroyOther}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyPrevious, Action::AddNewToFoo}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroySelf, Action::InvokePrevious}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroySelf, Action::InvokeSelf}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroySelf, Action::InvokeNext}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroySelf, Action::InvokeOther}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroySelf, Action::ProcessFoo}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroySelf, Action::ProcessBar}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroySelf, Action::DestroyPrevious}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroySelf, Action::DestroySelf}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroySelf, Action::DestroyNext}, {"previous", "self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroySelf, Action::DestroyOther}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroySelf, Action::AddNewToFoo}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyNext, Action::InvokePrevious}, {"previous", "self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyNext, Action::InvokeSelf}, {"previous", "self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyNext, Action::InvokeNext}, {"previous", "self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyNext, Action::InvokeOther}, {"previous", "self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyNext, Action::ProcessFoo}, {"previous", "self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyNext, Action::ProcessBar}, {"previous", "self", "other"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyNext, Action::DestroyPrevious}, {"previous", "self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyNext, Action::DestroySelf}, {"previous", "self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyNext, Action::DestroyNext}, {"previous", "self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyNext, Action::DestroyOther}, {"previous", "self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyNext, Action::AddNewToFoo}, {"previous", "self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyOther, Action::InvokePrevious}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyOther, Action::InvokeSelf}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyOther, Action::InvokeNext}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyOther, Action::InvokeOther}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyOther, Action::ProcessFoo}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyOther, Action::ProcessBar}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyOther, Action::DestroyPrevious}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyOther, Action::DestroySelf}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyOther, Action::DestroyNext}, {"previous", "self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyOther, Action::DestroyOther}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::DestroyOther, Action::AddNewToFoo}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::AddNewToFoo, Action::InvokePrevious}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::AddNewToFoo, Action::InvokeSelf}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::AddNewToFoo, Action::InvokeNext}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::AddNewToFoo, Action::InvokeOther}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::AddNewToFoo, Action::ProcessFoo}, {"previous", "self", "new0", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::AddNewToFoo, Action::ProcessBar}, {"previous", "self", "other", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::AddNewToFoo, Action::DestroyPrevious}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::AddNewToFoo, Action::DestroySelf}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::AddNewToFoo, Action::DestroyNext}, {"previous", "self"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::AddNewToFoo, Action::DestroyOther}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
  ActionsParameter({Action::ProcessFoo, Action::AddNewToFoo, Action::AddNewToFoo}, {"previous", "self", "next"}, OtherActionsExecution::WithinFirstAction),
};

class ClusterDiscoveryTest : public ActionExecutorTest {};

INSTANTIATE_TEST_SUITE_P(ClusterDiscoveryTestActions, ClusterDiscoveryTest,
                         testing::ValuesIn(all_actions));

TEST_P(ClusterDiscoveryTest, TestActions) {
  (void)generateActionsParameters;
  runTest();
}

/*
std::vector<ActionsParameter> generateSimpleActionsParameters() {
  ActionsCombinator combinator(all_simple_first_actions, all_simple_other_actions, 1);
  return generateActionsParameters(combinator, OtherActionsExecution::AfterFirstAction);
}

INSTANTIATE_TEST_SUITE_P(ClusterDiscoveryTestTwoActions, ClusterDiscoveryManagerSimpleTest,
                         testing::ValuesIn(generateSimpleActionsParameters()));

TEST_P(ClusterDiscoveryManagerSimpleTest, TestActions) { runTest(); }

class ClusterDiscoveryManagerComplexTest : public ActionExecutorTest {};

std::vector<ActionsParameter> generateComplexActionsParameters() {
  ActionsCombinator combinator(all_complex_first_actions, all_complex_other_actions, 2);
  return generateActionsParameters(combinator, OtherActionsExecution::WithinFirstAction);
}

INSTANTIATE_TEST_SUITE_P(ClusterDiscoveryTestTwoActionsWithinFirstAction,
                         ClusterDiscoveryManagerComplexTest,
                         testing::ValuesIn(generateComplexActionsParameters()));

TEST_P(ClusterDiscoveryManagerComplexTest, TestActions) { runTest(); }
*/

/*
class ClusterDiscoveryManagerTest : public testing::Test {
public:
  ClusterDiscoveryManagerTest()
    : lifecycle_handler_ (), cdm_("test_thread", lifecycle_handler_) {}

  ClusterDiscoveryManager::AddedCallbackData addCallback(std::string name) {
    return cdm_.addCallback(std::move(name),
std::make_unique<ClusterDiscoveryCallback>([this](ClusterDiscoveryStatus) {
      // we ignore the status, it's a thing that always comes from outside the manager
      ++callback_call_count_;
    }));
  }

  ClusterDiscoveryManager::AddedCallbackData addFailCallback(std::string name, std::string message)
{ return cdm_.addCallback(std::move(name), std::make_unique<ClusterDiscoveryCallback>([message =
std::move(message)](ClusterDiscoveryStatus) { ADD_FAILURE() << ("this callback should not be
invoked: " + message);
    }));
  }

  void processClusterName(std::string name) {
    auto cluster = MockThreadLocalCluster();
    cluster.cluster_.info_->name_ = std::move(name);
    lifecycle_handler_.invokeClusterAdded(cluster);
  }

  void useInvoker(ClusterDiscoveryManager::CallbackInvoker& invoker) {
    invoker.invokeCallback(ClusterDiscoveryStatus::Available);
  }

  TestClusterLifecycleCallbackHandler lifecycle_handler_;
  ClusterDiscoveryManager cdm_;
  unsigned callback_call_count_ = 0;
};


// Test the usual scenario where we request the discovery and the discovery manager gets notified,
// when the discovery process finishes.
TEST_F(ClusterDiscoveryManagerTest, TestHappyPath) {
  auto [handle, discovery_in_progress, invoker] = addCallback("foo");
  EXPECT_FALSE (discovery_in_progress);
  auto [handle2, discovery_in_progress2, invoker2] = addCallback("foo");
  EXPECT_TRUE (discovery_in_progress2);
  auto [handle3, discovery_in_progress3, invoker3] = addCallback("bar");
  EXPECT_FALSE (discovery_in_progress3);

  processClusterName("foo");
  EXPECT_EQ(callback_call_count_, 2);
  processClusterName("bar");
  EXPECT_EQ(callback_call_count_, 3);
}

// Test a case where discovery manager gets called on some irrelevant cluster being added or
// updated.
TEST_F(ClusterDiscoveryManagerTest, TestIrrelevantCluster) {
  auto [handle, discovery_in_progress, invoker] = addCallback("foo");

  processClusterName("bar");
  EXPECT_EQ(callback_call_count_, 0);
}

// Test the usual behavior around destroying the handles (before the discovery manager processes a
// cluster name and after).
TEST_F(ClusterDiscoveryManagerTest, TestHandleDestroyAroundProcessing) {
  auto [handle, discovery_in_progress, invoker] = addCallback("foo");
  auto [handle2, discovery_in_progress2, invoker2] = addFailCallback("foo", "this callback should be
dropped, because its handle was destroyed");

  handle2.reset();
  processClusterName("foo");
  EXPECT_EQ(callback_call_count_, 1);
  handle.reset();
  EXPECT_EQ(callback_call_count_, 1);
}

// Test the usual behavior around destroying the handles (before the invoker is used and after).
TEST_F(ClusterDiscoveryManagerTest, TestHandleDestroyAroundInvokers) {
  auto [handle, discovery_in_progress, invoker] = addCallback("foo");
  auto [handle2, discovery_in_progress2, invoker2] = addFailCallback("foo", "this callback should be
dropped, because its handle was destroyed");

  handle2.reset();
  useInvoker(invoker);
  EXPECT_EQ(callback_call_count_, 1);
  useInvoker(invoker2);
  EXPECT_EQ(callback_call_count_, 1);
  handle.reset();
  EXPECT_EQ(callback_call_count_, 1);
}

// Ensure that using the invoker twice calls the callback only once.
TEST_F(ClusterDiscoveryManagerTest, TestDoubleInvokerUse) {
  auto [handle, discovery_in_progress, invoker] = addCallback("foo");

  useInvoker(invoker);
  EXPECT_EQ(callback_call_count_, 1);
  useInvoker(invoker);
  EXPECT_EQ(callback_call_count_, 1);
}

// Ensure that processing the cluster name twice calls the callback only once.
TEST_F(ClusterDiscoveryManagerTest, TestDoubleProcessing) {
  auto [handle, discovery_in_progress, invoker] = addCallback("foo");

  processClusterName("foo");
  EXPECT_EQ(callback_call_count_, 1);
  processClusterName("foo");
  EXPECT_EQ(callback_call_count_, 1);
}

// Ensure that processing the cluster name is a noop if invoker was already called.
TEST_F(ClusterDiscoveryManagerTest, TestNoopProcessingAfterInvoking) {
  auto [handle, discovery_in_progress, invoker] = addCallback("foo");

  useInvoker(invoker);
  EXPECT_EQ(callback_call_count_, 1);
  processClusterName("foo");
  EXPECT_EQ(callback_call_count_, 1);
}

// Ensure that the invoker is a noop if processing the cluster name already happened.
TEST_F(ClusterDiscoveryManagerTest, TestNoopInvokingAfterProcessing) {
  auto [handle, discovery_in_progress, invoker] = addCallback("foo");

  processClusterName("foo");
  EXPECT_EQ(callback_call_count_, 1);
  useInvoker(invoker);
  EXPECT_EQ(callback_call_count_, 1);
}

// Test destroying own handle inside the callback while processing.
TEST_F(ClusterDiscoveryManagerTest, TestHandleDestroyInsideCallbackWhenProcessing) {
  ClusterDiscoveryCallbackHandlePtr* ptr_to_handle_ptr = nullptr;
  auto cb = std::make_unique<ClusterDiscoveryCallback>([this,
&ptr_to_handle_ptr](ClusterDiscoveryStatus) mutable { EXPECT_NE(ptr_to_handle_ptr, nullptr);
    // Don't crash on failure.
    if (ptr_to_handle_ptr != nullptr) {
      ptr_to_handle_ptr->reset();
    }
    ++callback_call_count_;
  });
  auto [handle, discovery_in_progress, invoker] = cdm_.addCallback("foo", std::move(cb));
  ptr_to_handle_ptr = &handle;

  processClusterName("foo");
  EXPECT_EQ(callback_call_count_, 1);
}

// Test destroying own handle inside the callback while invoking.
TEST_F(ClusterDiscoveryManagerTest, TestHandleDestroyInsideCallbackWhenInvoking) {
  ClusterDiscoveryCallbackHandlePtr* ptr_to_handle_ptr = nullptr;
  auto cb = std::make_unique<ClusterDiscoveryCallback>([this,
&ptr_to_handle_ptr](ClusterDiscoveryStatus) mutable { EXPECT_NE(ptr_to_handle_ptr, nullptr);
    // Don't crash on failure.
    if (ptr_to_handle_ptr != nullptr) {
      ptr_to_handle_ptr->reset();
    }
    ++callback_call_count_;
  });
  auto [handle, discovery_in_progress, invoker] = cdm_.addCallback("foo", std::move(cb));
  ptr_to_handle_ptr = &handle;

  useInvoker(invoker);
  EXPECT_EQ(callback_call_count_, 1);
}

// Ensure that destroying the handle clears the callback even if the callback is about to be
// invoked.
TEST_F(ClusterDiscoveryManagerTest, TestHandleDestroyInsideOtherCallback) {
  ClusterDiscoveryCallbackHandlePtr* ptr_to_handle_ptr = nullptr;
  auto cb = std::make_unique<ClusterDiscoveryCallback>([this,
&ptr_to_handle_ptr](ClusterDiscoveryStatus) mutable { EXPECT_NE(ptr_to_handle_ptr, nullptr);
    // Don't crash on failure.
    if (ptr_to_handle_ptr != nullptr) {
      ptr_to_handle_ptr->reset();
    }
    ++callback_call_count_;
  });
  auto [handle, discovery_in_progress, invoker] = cdm_.addCallback("foo", std::move(cb));
  auto [handle2, discovery_in_progress2, invoker2] = addFailCallback("foo", "this callback should be
invalidated just before invoking it"); ptr_to_handle_ptr = &handle2;

  processClusterName("foo");
  EXPECT_EQ(callback_call_count_, 1);
}

// Ensure that invoking a callback while inside another invoked callback clears the callback even if
// the callback is about to be invoked.
TEST_F(ClusterDiscoveryManagerTest, TestInvokingCallbackInsideOtherCallback) {
  ClusterDiscoveryManager::CallbackInvoker* ptr_to_invoker = nullptr;
  auto cb = std::make_unique<ClusterDiscoveryCallback>([this,
&ptr_to_invoker](ClusterDiscoveryStatus) mutable { EXPECT_NE(ptr_to_invoker, nullptr);
    // Don't crash on failure.
    if (ptr_to_invoker != nullptr) {
      useInvoker(*ptr_to_invoker);
    }
    ++callback_call_count_;
  });
  auto [handle, discovery_in_progress, invoker] = cdm_.addCallback("foo", std::move(cb));
  auto [handle2, discovery_in_progress2, invoker2] = addCallback("foo");
  ptr_to_invoker = &invoker2;

  processClusterName("foo");
  EXPECT_EQ(callback_call_count_, 2);
}

// Ensure that destroying the handle clears the callback even if the callback is about to be
// invoked. And using an invoker right after that is also a noop.
TEST_F(ClusterDiscoveryManagerTest, TestHandleDestroyInsideOtherCallbackAndInvokes) {
  ClusterDiscoveryCallbackHandlePtr* ptr_to_handle_ptr = nullptr;
  ClusterDiscoveryManager::CallbackInvoker* ptr_to_invoker = nullptr;
  auto cb = std::make_unique<ClusterDiscoveryCallback>([this, &ptr_to_handle_ptr,
&ptr_to_invoker](ClusterDiscoveryStatus) mutable { EXPECT_NE(ptr_to_handle_ptr, nullptr);
    EXPECT_NE(ptr_to_invoker, nullptr);
    // Don't crash on failure.
    if (ptr_to_handle_ptr != nullptr) {
      ptr_to_handle_ptr->reset();
    }
    if (ptr_to_invoker != nullptr) {
      useInvoker(*ptr_to_invoker);
    }
    ++callback_call_count_;
  });
  auto [handle, discovery_in_progress, invoker] = cdm_.addCallback("foo", std::move(cb));
  auto [handle2, discovery_in_progress2, invoker2] = addFailCallback("foo", "this callback should be
invalidated just before invoking it"); ptr_to_handle_ptr = &handle2; ptr_to_invoker = &invoker2;

  processClusterName("foo");
  EXPECT_EQ(callback_call_count_, 1);
}

// Ensure that destroying the handle clears the callback even if the callback is about to be
// invoked. And using an invoker right after that is also a noop.
// Ensure that invoking the callback TODO
TEST_F(ClusterDiscoveryManagerTest, TestHandleDestroyInsideOtherCallbackAndInvokes2) {
  ClusterDiscoveryCallbackHandlePtr* ptr_to_handle_ptr = nullptr;
  ClusterDiscoveryManager::CallbackInvoker* ptr_to_invoker = nullptr;
  auto cb = std::make_unique<ClusterDiscoveryCallback>([this, &ptr_to_handle_ptr,
&ptr_to_invoker](ClusterDiscoveryStatus) mutable { EXPECT_NE(ptr_to_handle_ptr, nullptr);
    EXPECT_NE(ptr_to_invoker, nullptr);
    // Don't crash on failure.
    if (ptr_to_handle_ptr != nullptr) {
      ptr_to_handle_ptr->reset();
    }
    if (ptr_to_invoker != nullptr) {
      useInvoker(*ptr_to_invoker);
    }
    ++callback_call_count_;
  });
  auto [handle, discovery_in_progress, invoker] = cdm_.addCallback("foo", std::move(cb));
  auto [handle2, discovery_in_progress2, invoker2] = addFailCallback("foo", "this callback should be
invalidated just before invoking it"); ptr_to_handle_ptr = &handle2; ptr_to_invoker = &invoker2;

  processClusterName("foo");
  EXPECT_EQ(callback_call_count_, 1);
}
*/

} // namespace
} // namespace Upstream
} // namespace Envoy
