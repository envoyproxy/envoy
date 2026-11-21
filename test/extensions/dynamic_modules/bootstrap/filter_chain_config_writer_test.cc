#include "envoy/config/listener/v3/listener_components.pb.h"

#include "source/common/stats/utility.h"
#include "source/extensions/bootstrap/dynamic_modules/filter_chain_config_writer.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/listener_manager.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {
namespace {

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

constexpr auto Success =
    envoy_dynamic_module_type_bootstrap_filter_chain_config_writer_update_result_Success;
constexpr auto NotAllowed =
    envoy_dynamic_module_type_bootstrap_filter_chain_config_writer_update_result_NotAllowed;
constexpr auto RemovalSuccess =
    envoy_dynamic_module_type_bootstrap_filter_chain_config_writer_removal_result_Success;
constexpr auto RemovalNotAllowed =
    envoy_dynamic_module_type_bootstrap_filter_chain_config_writer_removal_result_NotAllowed;
constexpr auto RemovalNotFound =
    envoy_dynamic_module_type_bootstrap_filter_chain_config_writer_removal_result_NotFound;
constexpr auto Active =
    envoy_dynamic_module_type_bootstrap_filter_chain_config_writer_update_status_Active;
constexpr auto Superseded =
    envoy_dynamic_module_type_bootstrap_filter_chain_config_writer_update_status_Superseded;
constexpr auto Removed =
    envoy_dynamic_module_type_bootstrap_filter_chain_config_writer_update_status_Removed;
constexpr auto Failed =
    envoy_dynamic_module_type_bootstrap_filter_chain_config_writer_update_status_Failed;

using FilterChainProto = envoy::config::listener::v3::FilterChain;

struct Outcome {
  uint64_t update_id;
  std::string filter_chain_name;
  envoy_dynamic_module_type_bootstrap_filter_chain_config_writer_update_status status;
  std::string error_details;
};

std::string serialize(const FilterChainProto& filter_chain) {
  std::string serialized;
  EXPECT_TRUE(filter_chain.SerializeToString(&serialized));
  return serialized;
}

class CountingListenerUpdateCallbacksHandle : public Server::ListenerUpdateCallbacksHandle {
public:
  explicit CountingListenerUpdateCallbacksHandle(int& releases) : releases_(releases) {}
  ~CountingListenerUpdateCallbacksHandle() override { ++releases_; }

private:
  int& releases_;
};

FilterChainProto filterChainProto(absl::string_view name, uint32_t connect_timeout_seconds = 1) {
  FilterChainProto filter_chain;
  filter_chain.set_name(name);
  filter_chain.mutable_transport_socket_connect_timeout()->set_seconds(connect_timeout_seconds);
  return filter_chain;
}

class FilterChainConfigWriterTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  FilterChainConfigWriterTest() {
    ON_CALL(context_.init_manager_, add(_))
        .WillByDefault(Invoke([this](const Init::Target& target) {
          init_target_handle_ = target.createHandle("test");
        }));
    // The tests that remove a filter chain expect it by name.
    EXPECT_CALL(listener_manager_, removeFilterChain(_)).Times(0);
  }

  void createWriter() {
    // The writer creates its apply callback first, and the newest expectation matches first.
    deliver_callback_ = new NiceMock<Event::MockSchedulableCallback>(&context_.dispatcher_);
    apply_callback_ = new NiceMock<Event::MockSchedulableCallback>(&context_.dispatcher_);
    writer_ = std::make_unique<DynamicModuleFilterChainConfigWriter>(
        "writer", context_,
        [this](uint64_t update_id, absl::string_view filter_chain_name,
               envoy_dynamic_module_type_bootstrap_filter_chain_config_writer_update_status status,
               absl::string_view error_details) {
          outcomes_.push_back(
              {update_id, std::string(filter_chain_name), status, std::string(error_details)});
        });
  }

  // Creates a writer that accepts writes and applies them.
  void createInitializedWriter() {
    createWriter();
    writer_->onServerInitialized(listener_manager_);
    init_target_handle_->initialize(init_watcher_);
  }

  uint64_t write(absl::string_view filter_chain_name, uint32_t connect_timeout_seconds = 1) {
    uint64_t update_id = 0;
    EXPECT_EQ(Success, writer_->addOrUpdateFilterChain(
                           filter_chain_name,
                           serialize(filterChainProto(filter_chain_name, connect_timeout_seconds)),
                           update_id));
    return update_id;
  }

  void runApplyPass() { apply_callback_->invokeCallback(); }

  // Delivers the outcomes that the writer reported and returns them.
  std::vector<Outcome> deliveredOutcomes() {
    if (deliver_callback_->enabled_) {
      deliver_callback_->invokeCallback();
    }
    std::vector<Outcome> outcomes;
    outcomes.swap(outcomes_);
    return outcomes;
  }

  uint64_t counter(absl::string_view name) {
    Stats::StatNameDynamicPool pool(context_.store_.symbolTable());
    const Stats::StatNameTagVector tags{{pool.add("config_name"), pool.add("writer")}};
    return Stats::Utility::counterFromElements(context_.serverScope(), elements(name), tags)
        .value();
  }

  uint64_t gauge(absl::string_view name) {
    Stats::StatNameDynamicPool pool(context_.store_.symbolTable());
    Stats::StatNameTagVector tags{{pool.add("config_name"), pool.add("writer")}};
    return Stats::Utility::gaugeFromElements(context_.serverScope(), elements(name),
                                             Stats::Gauge::ImportMode::NeverImport, tags)
        .value();
  }

  static Stats::ElementVec elements(absl::string_view name) {
    return {Stats::DynamicName("dynamic_modules"), Stats::DynamicName("filter_chain_config_writer"),
            Stats::DynamicName(name)};
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<Server::MockListenerManager> listener_manager_;
  NiceMock<Init::ExpectableWatcherImpl> init_watcher_;
  Init::TargetHandlePtr init_target_handle_;
  Event::MockSchedulableCallback* apply_callback_{};
  Event::MockSchedulableCallback* deliver_callback_{};
  std::unique_ptr<DynamicModuleFilterChainConfigWriter> writer_;
  std::vector<Outcome> outcomes_;
};

MATCHER_P2(IsOutcome, update_id, status, "") {
  return arg.update_id == update_id && arg.status == status;
}

TEST_F(FilterChainConfigWriterTest, RejectsWritesBeforeServerInitialized) {
  createWriter();
  uint64_t update_id = 1;
  EXPECT_EQ(NotAllowed, writer_->addOrUpdateFilterChain(
                            "writer.a", serialize(filterChainProto("writer.a")), update_id));
  EXPECT_EQ(0, update_id);
  EXPECT_EQ(RemovalNotAllowed, writer_->removeFilterChain("writer.a"));
  EXPECT_EQ(1, counter("update_rejected"));
}

TEST_F(FilterChainConfigWriterTest, ShutdownRefusesWritesAndDropsQueuedWork) {
  int handle_releases = 0;
  EXPECT_CALL(listener_manager_, addListenerUpdateCallbacks_(_))
      .WillOnce(Return(new CountingListenerUpdateCallbacksHandle(handle_releases)));
  createInitializedWriter();
  write("writer.b");
  EXPECT_CALL(listener_manager_, addOrUpdateFilterChain(_)).WillOnce(Return(true));
  runApplyPass();
  EXPECT_EQ(1, gauge("filter_chains_owned"));
  write("writer.a");
  // The second write leaves an undelivered Superseded outcome.
  write("writer.a", 2);
  EXPECT_CALL(*apply_callback_, cancel());
  EXPECT_CALL(*deliver_callback_, cancel());
  writer_->onShutdown();
  EXPECT_EQ(1, handle_releases);
  EXPECT_EQ(0, gauge("filter_chains_owned"));
  EXPECT_EQ(0, gauge("updates_pending"));

  uint64_t update_id = 0;
  EXPECT_EQ(NotAllowed, writer_->addOrUpdateFilterChain(
                            "writer.a", serialize(filterChainProto("writer.a")), update_id));
  EXPECT_EQ(RemovalNotAllowed, writer_->removeFilterChain("writer.a"));

  // Callbacks that fire anyway find no work.
  EXPECT_CALL(listener_manager_, addOrUpdateFilterChain(_)).Times(0);
  apply_callback_->enabled_ = true;
  runApplyPass();
  deliver_callback_->enabled_ = true;
  EXPECT_TRUE(deliveredOutcomes().empty());
}

TEST_F(FilterChainConfigWriterTest, AddsItsInitTargetWhenTheServerIsInitialized) {
  createWriter();
  EXPECT_EQ(nullptr, init_target_handle_);
  writer_->onServerInitialized(listener_manager_);
  ASSERT_NE(nullptr, init_target_handle_);
  EXPECT_CALL(listener_manager_, addListenerUpdateCallbacks_(_));
  EXPECT_CALL(init_watcher_, ready());
  init_target_handle_->initialize(init_watcher_);
}

TEST_F(FilterChainConfigWriterTest, StartsWithoutAnInitTargetOnceTheServerInitManagerStarted) {
  ON_CALL(context_.init_manager_, state()).WillByDefault(Return(Init::Manager::State::Initialized));
  EXPECT_CALL(context_.init_manager_, add(_)).Times(0);
  createWriter();
  EXPECT_CALL(listener_manager_, addListenerUpdateCallbacks_(_));
  writer_->onServerInitialized(listener_manager_);
  const uint64_t update_id = write("writer.a");
  EXPECT_CALL(listener_manager_, addOrUpdateFilterChain(_)).WillOnce(Return(false));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(update_id, Active)));
}

TEST_F(FilterChainConfigWriterTest, ShutdownBeforeClusterInitializationSkipsTheListenerManager) {
  createWriter();
  writer_->onServerInitialized(listener_manager_);
  writer_->onShutdown();
  EXPECT_CALL(listener_manager_, addListenerUpdateCallbacks_(_)).Times(0);
  EXPECT_CALL(init_watcher_, ready());
  init_target_handle_->initialize(init_watcher_);
}

TEST_F(FilterChainConfigWriterTest, WritesWaitForClusterInitialization) {
  createWriter();
  writer_->onServerInitialized(listener_manager_);
  write("writer.a");
  EXPECT_FALSE(apply_callback_->enabled_);

  EXPECT_CALL(*apply_callback_, scheduleCallbackNextIteration());
  init_target_handle_->initialize(init_watcher_);
  EXPECT_CALL(listener_manager_, addOrUpdateFilterChain(_)).WillOnce(Return(true));
  runApplyPass();
}

TEST_F(FilterChainConfigWriterTest, WriteIsActiveOnceEnvoyPostsTheFilterChainToTheWorkers) {
  createInitializedWriter();
  const uint64_t update_id = write("writer.a");
  EXPECT_EQ(1, update_id);
  EXPECT_CALL(listener_manager_, addOrUpdateFilterChain(_))
      .WillOnce(Invoke([](const FilterChainProto& filter_chain) -> absl::StatusOr<bool> {
        EXPECT_EQ("writer.a", filter_chain.name());
        return true;
      }));
  runApplyPass();
  EXPECT_TRUE(deliveredOutcomes().empty());
  EXPECT_EQ(1, gauge("updates_pending"));
  EXPECT_EQ(1, gauge("filter_chains_owned"));

  writer_->onFilterChainAddOrUpdate("writer.a");
  const std::vector<Outcome> outcomes = deliveredOutcomes();
  ASSERT_EQ(1, outcomes.size());
  EXPECT_THAT(outcomes[0], IsOutcome(update_id, Active));
  EXPECT_EQ("writer.a", outcomes[0].filter_chain_name);
  EXPECT_EQ("", outcomes[0].error_details);
  EXPECT_EQ(1, counter("update_submitted"));
  EXPECT_EQ(1, counter("update_active"));
  EXPECT_EQ(0, gauge("updates_pending"));
}

TEST_F(FilterChainConfigWriterTest, ChangesOfOtherFilterChainsAndListenersAreIgnored) {
  createInitializedWriter();
  const uint64_t update_id = write("writer.a");
  EXPECT_CALL(listener_manager_, addOrUpdateFilterChain(_)).WillOnce(Return(true));
  runApplyPass();
  NiceMock<Network::MockListenerConfig> listener_config;
  writer_->onListenerAddOrUpdate("writer.a", listener_config);
  writer_->onListenerRemoval("writer.a");
  writer_->onFilterChainAddOrUpdate("writer.b");
  EXPECT_TRUE(deliveredOutcomes().empty());

  writer_->onFilterChainAddOrUpdate("writer.a");
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(update_id, Active)));
}

TEST_F(FilterChainConfigWriterTest, UnchangedWriteIsActive) {
  createInitializedWriter();
  const uint64_t update_id = write("writer.a");
  EXPECT_CALL(listener_manager_, addOrUpdateFilterChain(_)).WillOnce(Return(false));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(update_id, Active)));
}

TEST_F(FilterChainConfigWriterTest, UnchangedWriteSupersedesTheWarmingWrite) {
  createInitializedWriter();
  const uint64_t first_update_id = write("writer.a");
  EXPECT_CALL(listener_manager_, addOrUpdateFilterChain(_))
      .WillOnce(Return(true))
      .WillOnce(Return(false));
  runApplyPass();
  const uint64_t second_update_id = write("writer.a");
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(first_update_id, Superseded)));

  writer_->onFilterChainAddOrUpdate("writer.a");
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(second_update_id, Active)));
  EXPECT_EQ(0, gauge("updates_pending"));
}

TEST_F(FilterChainConfigWriterTest, WarmingWriteIsSupersededByTheNextWrite) {
  createInitializedWriter();
  const uint64_t first_update_id = write("writer.a");
  EXPECT_CALL(listener_manager_, addOrUpdateFilterChain(_)).WillRepeatedly(Return(true));
  runApplyPass();
  const uint64_t second_update_id = write("writer.a", 2);
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(first_update_id, Superseded)));

  writer_->onFilterChainAddOrUpdate("writer.a");
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(second_update_id, Active)));
}

// The listener manager publishes the warming version before it applies the next one, so the next
// write keeps warming.
TEST_F(FilterChainConfigWriterTest, PublishOfTheWarmingWriteInsideTheNextWriteCompletesIt) {
  createInitializedWriter();
  const uint64_t first_update_id = write("writer.a");
  EXPECT_CALL(listener_manager_, addOrUpdateFilterChain(_))
      .WillOnce(Return(true))
      .WillOnce(Invoke([this](const FilterChainProto& filter_chain) -> absl::StatusOr<bool> {
        writer_->onFilterChainAddOrUpdate(filter_chain.name());
        return true;
      }));
  runApplyPass();
  const uint64_t second_update_id = write("writer.a", 2);
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(first_update_id, Active)));

  writer_->onFilterChainAddOrUpdate("writer.a");
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(second_update_id, Active)));
}

// The listener manager publishes the warming version before it finds the next write identical, so
// both writes are active.
TEST_F(FilterChainConfigWriterTest, PublishOfTheWarmingWriteInsideAnUnchangedWriteCompletesBoth) {
  createInitializedWriter();
  const uint64_t first_update_id = write("writer.a");
  EXPECT_CALL(listener_manager_, addOrUpdateFilterChain(_))
      .WillOnce(Return(true))
      .WillOnce(Invoke([this](const FilterChainProto& filter_chain) -> absl::StatusOr<bool> {
        writer_->onFilterChainAddOrUpdate(filter_chain.name());
        return false;
      }));
  runApplyPass();
  const uint64_t second_update_id = write("writer.a");
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(first_update_id, Active),
                                                        IsOutcome(second_update_id, Active)));
  EXPECT_EQ(0, gauge("updates_pending"));
}

TEST_F(FilterChainConfigWriterTest, InvalidWritesFail) {
  createInitializedWriter();
  EXPECT_CALL(listener_manager_, addOrUpdateFilterChain(_)).Times(0);
  uint64_t update_id = 0;

  // The filter chain has no name.
  EXPECT_EQ(Success,
            writer_->addOrUpdateFilterChain("", serialize(filterChainProto("")), update_id));
  // The filter chain does not parse.
  EXPECT_EQ(Success, writer_->addOrUpdateFilterChain("writer.a",
                                                     std::string("\x0a\x05"
                                                                 "abc",
                                                                 5),
                                                     update_id));
  runApplyPass();

  const std::vector<Outcome> outcomes = deliveredOutcomes();
  ASSERT_EQ(2, outcomes.size());
  for (const Outcome& outcome : outcomes) {
    EXPECT_EQ(Failed, outcome.status);
  }
  EXPECT_EQ("filter chain names must not be empty", outcomes[0].error_details);
  EXPECT_EQ("failed to parse filter chain writer.a", outcomes[1].error_details);
  EXPECT_EQ(2, counter("update_failed"));
  EXPECT_EQ(0, gauge("filter_chains_owned"));
  EXPECT_EQ(RemovalNotFound, writer_->removeFilterChain("writer.a"));
}

TEST_F(FilterChainConfigWriterTest, WriteFailsWhenTheListenerManagerRejectsIt) {
  createInitializedWriter();
  const uint64_t first_update_id = write("writer.a");
  const uint64_t second_update_id = write("writer.b");
  EXPECT_CALL(listener_manager_, addOrUpdateFilterChain(_))
      .WillOnce(
          Return(absl::AlreadyExistsError("filter chain writer.a is discovered through FCDS")))
      .WillOnce(Invoke([](const FilterChainProto&) -> absl::StatusOr<bool> {
        throw EnvoyException("contents of the filter chain");
      }));
  runApplyPass();
  const std::vector<Outcome> outcomes = deliveredOutcomes();
  ASSERT_EQ(2, outcomes.size());
  EXPECT_THAT(outcomes[0], IsOutcome(first_update_id, Failed));
  EXPECT_EQ("filter chain writer.a is discovered through FCDS", outcomes[0].error_details);
  EXPECT_THAT(outcomes[1], IsOutcome(second_update_id, Failed));
  EXPECT_EQ("the listener manager rejected filter chain writer.b", outcomes[1].error_details);
  EXPECT_EQ(2, counter("update_failed"));
  EXPECT_EQ(0, gauge("filter_chains_owned"));
  EXPECT_EQ(RemovalNotFound, writer_->removeFilterChain("writer.a"));
}

TEST_F(FilterChainConfigWriterTest, RejectedWriteKeepsTheWarmingWrite) {
  createInitializedWriter();
  const uint64_t first_update_id = write("writer.a");
  EXPECT_CALL(listener_manager_, addOrUpdateFilterChain(_))
      .WillOnce(Return(true))
      .WillOnce(Return(absl::InvalidArgumentError("contents of the filter chain")));
  runApplyPass();
  const uint64_t second_update_id = write("writer.a", 2);
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(second_update_id, Failed)));

  writer_->onFilterChainAddOrUpdate("writer.a");
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(first_update_id, Active)));
}

TEST_F(FilterChainConfigWriterTest, RemovesAnOwnedFilterChain) {
  createInitializedWriter();
  write("writer.a");
  EXPECT_CALL(listener_manager_, addOrUpdateFilterChain(_)).WillOnce(Return(false));
  runApplyPass();
  deliveredOutcomes();

  EXPECT_EQ(RemovalSuccess, writer_->removeFilterChain("writer.a"));
  EXPECT_CALL(listener_manager_, removeFilterChain("writer.a")).WillOnce(Return(true));
  runApplyPass();
  EXPECT_EQ(0, gauge("filter_chains_owned"));
  EXPECT_EQ(RemovalNotFound, writer_->removeFilterChain("writer.a"));
}

TEST_F(FilterChainConfigWriterTest, RemovalCompletesTheWarmingWrite) {
  createInitializedWriter();
  const uint64_t update_id = write("writer.a");
  EXPECT_CALL(listener_manager_, addOrUpdateFilterChain(_)).WillOnce(Return(true));
  runApplyPass();
  writer_->removeFilterChain("writer.a");
  EXPECT_CALL(listener_manager_, removeFilterChain("writer.a")).WillOnce(Return(true));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(update_id, Removed)));
  EXPECT_EQ(1, counter("update_removed"));
}

TEST_F(FilterChainConfigWriterTest, RemovalDuringAFailedFirstApplyRemovesNothing) {
  createInitializedWriter();
  const uint64_t update_id = write("writer.a");
  EXPECT_CALL(listener_manager_, addOrUpdateFilterChain(_))
      .WillOnce(Invoke([this](const FilterChainProto&) -> absl::StatusOr<bool> {
        EXPECT_EQ(RemovalSuccess, writer_->removeFilterChain("writer.a"));
        return absl::InvalidArgumentError("contents of the filter chain");
      }));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(update_id, Failed)));
  EXPECT_CALL(listener_manager_, removeFilterChain(_)).Times(0);
  runApplyPass();
  EXPECT_EQ(RemovalNotFound, writer_->removeFilterChain("writer.a"));
}

// Like FCDS updates, the apply passes of the filter chain config writer hold back no xDS requests.
// The writer has no ownership statistics.
TEST_F(FilterChainConfigWriterTest, HoldsBackNoXdsRequestsAndHasNoOwnershipStatistics) {
  createInitializedWriter();
  write("writer.a");
  EXPECT_CALL(context_.xds_manager_, pause(testing::A<const std::vector<std::string>&>())).Times(0);
  EXPECT_CALL(listener_manager_, addOrUpdateFilterChain(_)).WillOnce(Return(true));
  runApplyPass();
  EXPECT_NE(nullptr,
            TestUtility::findCounter(
                context_.store_,
                "dynamic_modules.filter_chain_config_writer.update_submitted.config_name.writer"));
  for (absl::string_view name : {"update_ownership_lost", "ownership_lost"}) {
    SCOPED_TRACE(name);
    EXPECT_EQ(nullptr,
              TestUtility::findCounter(context_.store_,
                                       absl::StrCat("dynamic_modules.filter_chain_config_writer.",
                                                    name, ".config_name.writer")));
  }
}

TEST_F(FilterChainConfigWriterTest, RemovalDuringTheFirstApplyRemovesTheFilterChain) {
  createInitializedWriter();
  const uint64_t first_update_id = write("writer.a");
  uint64_t second_update_id = 0;
  EXPECT_CALL(listener_manager_, addOrUpdateFilterChain(_))
      .WillOnce(Invoke([&](const FilterChainProto&) -> absl::StatusOr<bool> {
        // A lifecycle hook of the module writes the filter chain again and removes it.
        second_update_id = write("writer.a", 2);
        EXPECT_EQ(RemovalSuccess, writer_->removeFilterChain("writer.a"));
        return true;
      }));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(second_update_id, Removed)));

  EXPECT_CALL(listener_manager_, removeFilterChain("writer.a")).WillOnce(Return(true));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(first_update_id, Removed)));
  EXPECT_EQ(0, gauge("filter_chains_owned"));
}

} // namespace
} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
