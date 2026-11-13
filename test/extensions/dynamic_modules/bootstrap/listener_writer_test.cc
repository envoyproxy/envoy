#include "envoy/config/listener/v3/listener.pb.h"

#include "source/common/common/cleanup.h"
#include "source/common/config/dependent_type_urls.h"
#include "source/common/config/resource_name.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stats/utility.h"
#include "source/extensions/bootstrap/dynamic_modules/listener_writer.h"
#include "source/extensions/bootstrap/dynamic_modules/writer_shared_state.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/listener_manager.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/logging.h"
#include "test/test_common/simulated_time_system.h"

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

constexpr auto Success = envoy_dynamic_module_type_bootstrap_listener_writer_update_result_Success;
constexpr auto NotAllowed =
    envoy_dynamic_module_type_bootstrap_listener_writer_update_result_NotAllowed;
constexpr auto RemovalSuccess =
    envoy_dynamic_module_type_bootstrap_listener_writer_removal_result_Success;
constexpr auto RemovalNotAllowed =
    envoy_dynamic_module_type_bootstrap_listener_writer_removal_result_NotAllowed;
constexpr auto RemovalNotFound =
    envoy_dynamic_module_type_bootstrap_listener_writer_removal_result_NotFound;
constexpr auto Active = envoy_dynamic_module_type_bootstrap_listener_writer_update_status_Active;
constexpr auto Superseded =
    envoy_dynamic_module_type_bootstrap_listener_writer_update_status_Superseded;
constexpr auto Removed = envoy_dynamic_module_type_bootstrap_listener_writer_update_status_Removed;
constexpr auto Failed = envoy_dynamic_module_type_bootstrap_listener_writer_update_status_Failed;
constexpr auto OwnershipLost =
    envoy_dynamic_module_type_bootstrap_listener_writer_update_status_OwnershipLost;

using ListenerProto = envoy::config::listener::v3::Listener;

struct Outcome {
  uint64_t update_id;
  std::string listener_name;
  envoy_dynamic_module_type_bootstrap_listener_writer_update_status status;
  std::string error_details;
};

std::string serialize(const Protobuf::Message& message) {
  std::string serialized;
  EXPECT_TRUE(message.SerializeToString(&serialized));
  return serialized;
}

class CountingListenerUpdateCallbacksHandle : public Server::ListenerUpdateCallbacksHandle {
public:
  explicit CountingListenerUpdateCallbacksHandle(int& releases) : releases_(releases) {}
  ~CountingListenerUpdateCallbacksHandle() override { ++releases_; }

private:
  int& releases_;
};

ListenerProto listenerProto(absl::string_view name, uint32_t port = 10000) {
  ListenerProto listener;
  listener.set_name(name);
  envoy::config::core::v3::SocketAddress* socket_address =
      listener.mutable_address()->mutable_socket_address();
  socket_address->set_address("127.0.0.1");
  socket_address->set_port_value(port);
  return listener;
}

class ListenerWriterTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  ListenerWriterTest() {
    ON_CALL(context_.init_manager_, add(_))
        .WillByDefault(Invoke([this](const Init::Target& target) {
          init_target_handle_ = target.createHandle("test");
        }));
    ON_CALL(listener_manager_, isWorkerStarted()).WillByDefault(Return(true));
    // The tests that remove a listener expect it by name.
    EXPECT_CALL(listener_manager_, removeListener(_)).Times(0);
  }

  void createWriter() {
    // The writer creates its apply callback first, and the newest expectation matches first.
    init_target_check_callback_ =
        new NiceMock<Event::MockSchedulableCallback>(&context_.dispatcher_);
    workers_started_callback_ = new NiceMock<Event::MockSchedulableCallback>(&context_.dispatcher_);
    deliver_callback_ = new NiceMock<Event::MockSchedulableCallback>(&context_.dispatcher_);
    apply_callback_ = new NiceMock<Event::MockSchedulableCallback>(&context_.dispatcher_);
    writer_ = std::make_unique<DynamicModuleListenerWriter>(
        "writer", context_,
        [this](uint64_t update_id, absl::string_view listener_name,
               envoy_dynamic_module_type_bootstrap_listener_writer_update_status status,
               absl::string_view error_details) {
          outcomes_.push_back(
              {update_id, std::string(listener_name), status, std::string(error_details)});
        });
  }

  // Creates a writer that accepts writes and applies them.
  void createInitializedWriter() {
    createWriter();
    writer_->onServerInitialized(listener_manager_);
    init_target_handle_->initialize(init_watcher_);
  }

  uint64_t write(absl::string_view listener_name, absl::string_view version_info = "v1",
                 uint32_t port = 10000) {
    uint64_t update_id = 0;
    EXPECT_EQ(Success, writer_->addOrUpdateListener(listener_name,
                                                    serialize(listenerProto(listener_name, port)),
                                                    version_info, update_id));
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

  void activate(absl::string_view listener_name) {
    writer_->onListenerAddOrUpdate(listener_name, listener_config_);
  }

  // Makes the listener manager activate a listener inside its call.
  void activateInline() {
    EXPECT_CALL(listener_manager_, addOrUpdateListener(_, _, true))
        .WillOnce(Invoke([this](const ListenerProto& listener, const std::string&,
                                bool) -> absl::StatusOr<bool> {
          activate(listener.name());
          return true;
        }));
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
    return {Stats::DynamicName("dynamic_modules"), Stats::DynamicName("listener_writer"),
            Stats::DynamicName(name)};
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<Server::MockListenerManager> listener_manager_;
  NiceMock<Network::MockListenerConfig> listener_config_;
  NiceMock<Init::ExpectableWatcherImpl> init_watcher_;
  Init::TargetHandlePtr init_target_handle_;
  Event::MockSchedulableCallback* apply_callback_{};
  Event::MockSchedulableCallback* deliver_callback_{};
  Event::MockSchedulableCallback* workers_started_callback_{};
  Event::MockSchedulableCallback* init_target_check_callback_{};
  std::unique_ptr<DynamicModuleListenerWriter> writer_;
  std::vector<Outcome> outcomes_;
};

MATCHER_P2(IsOutcome, update_id, status, "") {
  return arg.update_id == update_id && arg.status == status;
}

TEST_F(ListenerWriterTest, ShutdownRefusesWritesAndDropsQueuedWork) {
  int handle_releases = 0;
  EXPECT_CALL(listener_manager_, addListenerUpdateCallbacks_(_))
      .WillOnce(Return(new CountingListenerUpdateCallbacksHandle(handle_releases)));
  createInitializedWriter();
  write("writer.b");
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, "v1", true)).WillOnce(Return(true));
  runApplyPass();
  EXPECT_EQ(1, gauge("listeners_owned"));
  write("writer.a");
  // The second write leaves an undelivered Superseded outcome.
  write("writer.a", "v2");
  EXPECT_CALL(*apply_callback_, cancel());
  EXPECT_CALL(*deliver_callback_, cancel());
  EXPECT_CALL(*workers_started_callback_, cancel());
  EXPECT_CALL(*init_target_check_callback_, cancel());
  writer_->onShutdown();
  EXPECT_EQ(1, handle_releases);
  EXPECT_EQ(0, gauge("listeners_owned"));
  EXPECT_EQ(0, gauge("updates_pending"));

  uint64_t update_id = 0;
  EXPECT_EQ(NotAllowed, writer_->addOrUpdateListener(
                            "writer.a", serialize(listenerProto("writer.a")), "v1", update_id));
  EXPECT_EQ(RemovalNotAllowed, writer_->removeListener("writer.a"));

  // Callbacks that fire anyway find no work.
  EXPECT_CALL(*workers_started_callback_, scheduleCallbackNextIteration()).Times(0);
  writer_->onPostInit();
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, _, _)).Times(0);
  apply_callback_->enabled_ = true;
  runApplyPass();
  deliver_callback_->enabled_ = true;
  EXPECT_TRUE(deliveredOutcomes().empty());
}

TEST_F(ListenerWriterTest, InitTargetIsReadyOnceTheModuleInitialized) {
  createWriter();
  EXPECT_EQ(nullptr, init_target_handle_);
  writer_->onServerInitialized(listener_manager_);
  ASSERT_NE(nullptr, init_target_handle_);
  EXPECT_CALL(listener_manager_, addListenerUpdateCallbacks_(_));
  EXPECT_CALL(init_watcher_, ready()).Times(0);
  init_target_handle_->initialize(init_watcher_);
  testing::Mock::VerifyAndClearExpectations(&init_watcher_);

  EXPECT_CALL(init_watcher_, ready());
  writer_->onModuleInitialized();
  writer_->onModuleInitialized();
}

TEST_F(ListenerWriterTest, InitTargetIsReadyAtOnceWhenTheModuleInitializedFirst) {
  createWriter();
  writer_->onServerInitialized(listener_manager_);
  writer_->onModuleInitialized();
  EXPECT_CALL(init_watcher_, ready());
  init_target_handle_->initialize(init_watcher_);
}

TEST_F(ListenerWriterTest, InitTargetWaitsForTheWritesSubmittedBeforeTheModuleInitialized) {
  createInitializedWriter();
  // A write submitted before the module initialized holds back Envoy until it is applied, even
  // when the module writes the listener again afterwards.
  write("writer.a");
  EXPECT_CALL(init_watcher_, ready()).Times(0);
  writer_->onModuleInitialized();
  write("writer.a", "v2");
  write("writer.b");
  // A second signal keeps the cutoff of the first one.
  writer_->onModuleInitialized();
  testing::Mock::VerifyAndClearExpectations(&init_watcher_);

  // The write of writer.b came after the module initialized, so it does not hold back Envoy.
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, "v2", true))
      .WillOnce(
          Invoke([this](const ListenerProto&, const std::string&, bool) -> absl::StatusOr<bool> {
            simTime().advanceTimeWait(std::chrono::milliseconds(10));
            return true;
          }));
  EXPECT_CALL(init_watcher_, ready());
  runApplyPass();
}

TEST_F(ListenerWriterTest, RemovalOfAnEarlierWriteReleasesTheInitTarget) {
  createInitializedWriter();
  const uint64_t update_id = write("writer.a");
  EXPECT_CALL(init_watcher_, ready()).Times(0);
  writer_->onModuleInitialized();
  testing::Mock::VerifyAndClearExpectations(&init_watcher_);

  // The removal cancels the queued write, and Envoy finishes initializing in a later loop
  // iteration.
  EXPECT_CALL(init_watcher_, ready()).Times(0);
  EXPECT_EQ(RemovalSuccess, writer_->removeListener("writer.a"));
  testing::Mock::VerifyAndClearExpectations(&init_watcher_);
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(update_id, Removed)));
  EXPECT_EQ(RemovalNotFound, writer_->removeListener("writer.a"));
  EXPECT_CALL(init_watcher_, ready());
  init_target_check_callback_->invokeCallback();
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, _, _)).Times(0);
  runApplyPass();
}

// A writer that starts while Envoy initializes still holds back the initialization.
TEST_F(ListenerWriterTest, AddsItsInitTargetWhileTheServerInitManagerInitializes) {
  ON_CALL(context_.init_manager_, state())
      .WillByDefault(Return(Init::Manager::State::Initializing));
  EXPECT_CALL(context_.init_manager_, add(_));
  createWriter();
  writer_->onServerInitialized(listener_manager_);
  EXPECT_CALL(init_watcher_, ready()).Times(0);
  init_target_handle_->initialize(init_watcher_);
  testing::Mock::VerifyAndClearExpectations(&init_watcher_);
  EXPECT_CALL(init_watcher_, ready());
  writer_->onModuleInitialized();
}

TEST_F(ListenerWriterTest, StartsWithoutAnInitTargetOnceTheServerInitManagerFinished) {
  ON_CALL(context_.init_manager_, state()).WillByDefault(Return(Init::Manager::State::Initialized));
  EXPECT_CALL(context_.init_manager_, add(_)).Times(0);
  createWriter();
  EXPECT_CALL(listener_manager_, addListenerUpdateCallbacks_(_));
  writer_->onServerInitialized(listener_manager_);
  const uint64_t update_id = write("writer.a");
  activateInline();
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(update_id, Active)));
}

TEST_F(ListenerWriterTest, ShutdownBeforeListenerInitializationSkipsTheListenerManager) {
  createWriter();
  writer_->onServerInitialized(listener_manager_);
  writer_->onShutdown();
  EXPECT_CALL(listener_manager_, addListenerUpdateCallbacks_(_)).Times(0);
  init_target_handle_->initialize(init_watcher_);
}

TEST_F(ListenerWriterTest, WritesWaitForListenerInitialization) {
  createWriter();
  writer_->onServerInitialized(listener_manager_);
  write("writer.a");
  EXPECT_FALSE(apply_callback_->enabled_);

  EXPECT_CALL(*apply_callback_, scheduleCallbackNextIteration());
  init_target_handle_->initialize(init_watcher_);
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, "v1", true)).WillOnce(Return(true));
  runApplyPass();
}

TEST_F(ListenerWriterTest, WriteIsActiveOnceTheListenerWarms) {
  createInitializedWriter();
  const uint64_t update_id = write("writer.a");
  EXPECT_EQ(1, update_id);
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, "v1", true))
      .WillOnce(Invoke(
          [](const ListenerProto& listener, const std::string&, bool) -> absl::StatusOr<bool> {
            EXPECT_EQ("writer.a", listener.name());
            return true;
          }));
  runApplyPass();
  EXPECT_TRUE(deliveredOutcomes().empty());
  EXPECT_EQ(1, gauge("updates_pending"));
  EXPECT_EQ(1, gauge("listeners_owned"));

  activate("writer.a");
  const std::vector<Outcome> outcomes = deliveredOutcomes();
  ASSERT_EQ(1, outcomes.size());
  EXPECT_THAT(outcomes[0], IsOutcome(update_id, Active));
  EXPECT_EQ("writer.a", outcomes[0].listener_name);
  EXPECT_EQ("", outcomes[0].error_details);
  EXPECT_EQ(1, counter("update_submitted"));
  EXPECT_EQ(1, counter("update_active"));
  EXPECT_EQ(0, gauge("updates_pending"));

  // A write of the configuration that the warmed listener serves is active at once.
  const uint64_t unchanged_update_id = write("writer.a", "v2");
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, "v2", true)).WillOnce(Return(false));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(unchanged_update_id, Active)));
}

TEST_F(ListenerWriterTest, WriteIsActiveWhenTheListenerWarmsInline) {
  createInitializedWriter();
  const uint64_t update_id = write("writer.a");
  activateInline();
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(update_id, Active)));

  // The next write that does not activate inline waits for the listener to warm.
  const uint64_t second_update_id = write("writer.a", "v2", 10001);
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, "v2", true)).WillOnce(Return(true));
  runApplyPass();
  EXPECT_TRUE(deliveredOutcomes().empty());
  activate("writer.a");
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(second_update_id, Active)));
}

TEST_F(ListenerWriterTest, WriteBeforeTheWorkersStartIsActiveOnceTheyStart) {
  createInitializedWriter();
  ON_CALL(listener_manager_, isWorkerStarted()).WillByDefault(Return(false));
  const uint64_t update_id = write("writer.a");
  activateInline();
  runApplyPass();
  EXPECT_TRUE(deliveredOutcomes().empty());
  EXPECT_EQ(1, gauge("updates_pending"));

  EXPECT_CALL(*workers_started_callback_, scheduleCallbackNextIteration());
  writer_->onPostInit();
  ON_CALL(listener_manager_, isWorkerStarted()).WillByDefault(Return(true));
  workers_started_callback_->invokeCallback();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(update_id, Active)));
  EXPECT_EQ(0, gauge("updates_pending"));
}

TEST_F(ListenerWriterTest, WriteBeforeTheWorkersStartReplacesTheWriteThatWaitsForThem) {
  createInitializedWriter();
  ON_CALL(listener_manager_, isWorkerStarted()).WillByDefault(Return(false));
  const uint64_t first_update_id = write("writer.a");
  activateInline();
  runApplyPass();

  // A changed write replaces the active listener at once.
  const uint64_t second_update_id = write("writer.a", "v2", 10001);
  activateInline();
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(first_update_id, Superseded)));

  // The listener manager finds an identical write unchanged.
  const uint64_t third_update_id = write("writer.a", "v3", 10001);
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, "v3", true)).WillOnce(Return(false));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(second_update_id, Superseded)));

  writer_->onPostInit();
  ON_CALL(listener_manager_, isWorkerStarted()).WillByDefault(Return(true));
  workers_started_callback_->invokeCallback();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(third_update_id, Active)));
}

TEST_F(ListenerWriterTest, ApplyPassFirstCompletesTheWritesThatWaitForTheWorkers) {
  createInitializedWriter();
  ON_CALL(listener_manager_, isWorkerStarted()).WillByDefault(Return(false));
  const uint64_t first_update_id = write("writer.a");
  activateInline();
  runApplyPass();
  writer_->onPostInit();
  ON_CALL(listener_manager_, isWorkerStarted()).WillByDefault(Return(true));

  // An apply pass that runs before the workers started callback completes the waiting write
  // first.
  const uint64_t second_update_id = write("writer.a", "v2", 10001);
  EXPECT_CALL(*workers_started_callback_, cancel());
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, "v2", true)).WillOnce(Return(true));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(first_update_id, Active)));
  activate("writer.a");
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(second_update_id, Active)));
}

TEST_F(ListenerWriterTest, WorkersThatStartDuringAPassCompleteTheWritesThatWaitForThem) {
  createInitializedWriter();
  ON_CALL(listener_manager_, isWorkerStarted()).WillByDefault(Return(false));
  const uint64_t first_update_id = write("writer.a");
  activateInline();
  runApplyPass();

  const uint64_t second_update_id = write("writer.b");
  const uint64_t third_update_id = write("writer.a", "v2", 10001);
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, _, true))
      .WillOnce(Invoke(
          [this](const ListenerProto& listener, const std::string&, bool) -> absl::StatusOr<bool> {
            EXPECT_EQ("writer.b", listener.name());
            // The workers start inside this call.
            activate(listener.name());
            writer_->onPostInit();
            ON_CALL(listener_manager_, isWorkerStarted()).WillByDefault(Return(true));
            return true;
          }))
      .WillOnce(Invoke(
          [this](const ListenerProto& listener, const std::string&, bool) -> absl::StatusOr<bool> {
            activate(listener.name());
            return true;
          }));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(second_update_id, Active),
                                                        IsOutcome(first_update_id, Active),
                                                        IsOutcome(third_update_id, Active)));
}

TEST_F(ListenerWriterTest, WriteThatRevertsTheWarmingWriteIsActive) {
  createInitializedWriter();
  const uint64_t first_update_id = write("writer.a");
  activateInline();
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(first_update_id, Active)));

  const uint64_t second_update_id = write("writer.a", "v2", 10001);
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, _, true)).WillRepeatedly(Return(true));
  runApplyPass();
  // The listener manager drops the warming listener, since the active one matches this write.
  const uint64_t third_update_id = write("writer.a", "v3");
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(second_update_id, Superseded),
                                                        IsOutcome(third_update_id, Active)));
  EXPECT_EQ(0, gauge("updates_pending"));
}

TEST_F(ListenerWriterTest, UnchangedWriteIsActive) {
  createInitializedWriter();
  write("writer.a");
  activateInline();
  runApplyPass();
  deliveredOutcomes();

  const uint64_t update_id = write("writer.a", "v2");
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, "v2", true)).WillOnce(Return(false));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(update_id, Active)));
}

TEST_F(ListenerWriterTest, UnchangedWriteSupersedesTheIdenticalWarmingWrite) {
  createInitializedWriter();
  const uint64_t first_update_id = write("writer.a");
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, _, true))
      .WillOnce(Return(true))
      .WillOnce(Return(false));
  runApplyPass();
  const uint64_t second_update_id = write("writer.a", "v2");
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(first_update_id, Superseded)));

  activate("writer.a");
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(second_update_id, Active)));
  EXPECT_EQ(0, gauge("updates_pending"));
}

TEST_F(ListenerWriterTest, WriteFailsWhileItsListenersAreStopped) {
  createInitializedWriter();
  const uint64_t first_update_id = write("writer.a");
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, _, true))
      .WillOnce(Return(true))
      .WillOnce(Return(false));
  runApplyPass();
  const uint64_t second_update_id = write("writer.a", "v2", 10001);
  runApplyPass();
  const std::vector<Outcome> outcomes = deliveredOutcomes();
  ASSERT_EQ(1, outcomes.size());
  EXPECT_THAT(outcomes[0], IsOutcome(second_update_id, Failed));
  EXPECT_EQ("the listener manager refused listener writer.a, since its listeners are stopped",
            outcomes[0].error_details);

  // The warming write still completes.
  activate("writer.a");
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(first_update_id, Active)));
}

TEST_F(ListenerWriterTest, WriteOfANewListenerFailsWhileItsListenersAreStopped) {
  createInitializedWriter();
  const uint64_t update_id = write("writer.a");
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, "v1", true)).WillOnce(Return(false));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(update_id, Failed)));
  EXPECT_EQ(0, gauge("listeners_owned"));
  EXPECT_EQ(RemovalNotFound, writer_->removeListener("writer.a"));
}

TEST_F(ListenerWriterTest, WarmingWriteIsSupersededByTheNextWrite) {
  createInitializedWriter();
  const uint64_t first_update_id = write("writer.a");
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, _, true)).WillRepeatedly(Return(true));
  runApplyPass();
  const uint64_t second_update_id = write("writer.a", "v2", 10001);
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(first_update_id, Superseded)));

  activate("writer.a");
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(second_update_id, Active)));
}

TEST_F(ListenerWriterTest, InvalidWritesFail) {
  createInitializedWriter();
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, _, _)).Times(0);
  uint64_t update_id = 0;

  // The listener has no name.
  EXPECT_EQ(Success,
            writer_->addOrUpdateListener("", serialize(listenerProto("")), "v1", update_id));
  // The listener does not parse.
  EXPECT_EQ(Success, writer_->addOrUpdateListener("writer.a",
                                                  std::string("\x0a\x05"
                                                              "abc",
                                                              5),
                                                  "v1", update_id));
  // The listener is an API listener.
  ListenerProto listener = listenerProto("writer.c");
  listener.mutable_api_listener();
  EXPECT_EQ(Success,
            writer_->addOrUpdateListener("writer.c", serialize(listener), "v1", update_id));
  runApplyPass();

  const std::vector<Outcome> outcomes = deliveredOutcomes();
  ASSERT_EQ(3, outcomes.size());
  for (const Outcome& outcome : outcomes) {
    EXPECT_EQ(Failed, outcome.status);
  }
  EXPECT_EQ("listener names must not be empty", outcomes[0].error_details);
  EXPECT_EQ("failed to parse listener writer.a", outcomes[1].error_details);
  EXPECT_EQ("listener writer.c is an API listener", outcomes[2].error_details);
  EXPECT_EQ(3, counter("update_failed"));
  EXPECT_EQ(0, gauge("listeners_owned"));
  EXPECT_EQ(RemovalNotFound, writer_->removeListener("writer.a"));
  EXPECT_EQ(RemovalNotFound, writer_->removeListener("writer.c"));
}

TEST_F(ListenerWriterTest, WriteFailsWhenAnotherComponentOwnsTheName) {
  createInitializedWriter();
  const uint64_t update_id = write("writer.a");
  write("writer.b");
  listener_config_.name_ = "writer.a";
  EXPECT_CALL(listener_manager_,
              listeners(Server::ListenerManager::WARMING | Server::ListenerManager::ACTIVE))
      .WillRepeatedly(Return(std::vector<std::reference_wrapper<Network::ListenerConfig>>{
          std::ref<Network::ListenerConfig>(listener_config_)}));
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, _, true))
      .WillOnce(Invoke(
          [](const ListenerProto& listener, const std::string&, bool) -> absl::StatusOr<bool> {
            EXPECT_EQ("writer.b", listener.name());
            return true;
          }));
  runApplyPass();
  const std::vector<Outcome> outcomes = deliveredOutcomes();
  ASSERT_EQ(1, outcomes.size());
  EXPECT_THAT(outcomes[0], IsOutcome(update_id, Failed));
  EXPECT_EQ("listener writer.a exists and is not owned by this extension",
            outcomes[0].error_details);
  EXPECT_EQ(RemovalNotFound, writer_->removeListener("writer.a"));
}

// The extension updates a listener that it owns, which the listener manager reports, with its
// latest write.
TEST_F(ListenerWriterTest, UpdateOfAnOwnedListenerAppliesTheLatestWrite) {
  createInitializedWriter();
  write("writer.a");
  activateInline();
  runApplyPass();
  deliveredOutcomes();
  listener_config_.name_ = "writer.a";
  ON_CALL(listener_manager_, listeners(_))
      .WillByDefault(Return(std::vector<std::reference_wrapper<Network::ListenerConfig>>{
          std::ref<Network::ListenerConfig>(listener_config_)}));

  const uint64_t first_update_id = write("writer.a", "v2", 10001);
  const uint64_t second_update_id = write("writer.a", "v3", 10002);
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, "v3", true))
      .WillOnce(Invoke(
          [this](const ListenerProto& listener, const std::string&, bool) -> absl::StatusOr<bool> {
            EXPECT_EQ(10002, listener.address().socket_address().port_value());
            activate(listener.name());
            return true;
          }));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(first_update_id, Superseded),
                                                        IsOutcome(second_update_id, Active)));
}

TEST_F(ListenerWriterTest, WriteFailsWhenTheListenerManagerRejectsIt) {
  createInitializedWriter();
  const uint64_t first_update_id = write("writer.a");
  const uint64_t second_update_id = write("writer.b");
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, "v1", true))
      .WillOnce(Return(absl::InvalidArgumentError("contents of the listener")))
      .WillOnce(Invoke([](const ListenerProto&, const std::string&, bool) -> absl::StatusOr<bool> {
        throw EnvoyException("contents of the listener");
      }));
  runApplyPass();
  const std::vector<Outcome> outcomes = deliveredOutcomes();
  ASSERT_EQ(2, outcomes.size());
  EXPECT_THAT(outcomes[0], IsOutcome(first_update_id, Failed));
  EXPECT_EQ("the listener manager rejected listener writer.a", outcomes[0].error_details);
  EXPECT_THAT(outcomes[1], IsOutcome(second_update_id, Failed));
  EXPECT_EQ("the listener manager rejected listener writer.b", outcomes[1].error_details);
  EXPECT_EQ(2, counter("update_failed"));
  EXPECT_EQ(0, gauge("listeners_owned"));
  EXPECT_EQ(RemovalNotFound, writer_->removeListener("writer.a"));
}

TEST_F(ListenerWriterTest, RejectedWriteKeepsTheWarmingWrite) {
  createInitializedWriter();
  const uint64_t first_update_id = write("writer.a");
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, _, true))
      .WillOnce(Return(true))
      .WillOnce(Return(absl::InvalidArgumentError("contents of the listener")));
  runApplyPass();
  const uint64_t second_update_id = write("writer.a", "v2", 10001);
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(second_update_id, Failed)));

  activate("writer.a");
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(first_update_id, Active)));
}

TEST_F(ListenerWriterTest, RejectedWriteKeepsTheWriteThatWaitsForTheWorkers) {
  createInitializedWriter();
  ON_CALL(listener_manager_, isWorkerStarted()).WillByDefault(Return(false));
  const uint64_t first_update_id = write("writer.a");
  activateInline();
  runApplyPass();
  const uint64_t second_update_id = write("writer.a", "v2", 10001);
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, "v2", true))
      .WillOnce(Return(absl::InvalidArgumentError("contents of the listener")));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(second_update_id, Failed)));
  // A write that the listener manager refuses because its listeners are stopped keeps it too.
  const uint64_t third_update_id = write("writer.a", "v3", 10002);
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, "v3", true)).WillOnce(Return(false));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(third_update_id, Failed)));

  writer_->onPostInit();
  ON_CALL(listener_manager_, isWorkerStarted()).WillByDefault(Return(true));
  workers_started_callback_->invokeCallback();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(first_update_id, Active)));
}

TEST_F(ListenerWriterTest, ListenFailureInsideTheListenerManagerCallFailsTheWrite) {
  createInitializedWriter();
  const uint64_t first_update_id = write("writer.a");
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, "v1", true)).WillOnce(Return(true));
  runApplyPass();
  const uint64_t second_update_id = write("writer.a", "v2", 10001);
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, "v2", true))
      .WillOnce(Invoke(
          [this](const ListenerProto& listener, const std::string&, bool) -> absl::StatusOr<bool> {
            // The listener warms, fails to listen, and the listener manager removes it.
            writer_->onListenerListenFailure(listener.name(), /*removed=*/true);
            writer_->onListenerRemoval(listener.name());
            return true;
          }));
  runApplyPass();
  const std::vector<Outcome> outcomes = deliveredOutcomes();
  ASSERT_EQ(2, outcomes.size());
  EXPECT_THAT(outcomes[0], IsOutcome(first_update_id, Superseded));
  EXPECT_THAT(outcomes[1], IsOutcome(second_update_id, Failed));
  EXPECT_EQ("listener writer.a failed to listen and was removed", outcomes[1].error_details);
  EXPECT_EQ(0, counter("ownership_lost"));
  EXPECT_EQ(0, gauge("listeners_owned"));
  EXPECT_EQ(RemovalNotFound, writer_->removeListener("writer.a"));
}

// Destroying the replaced listener can start the workers inside the listener manager call, and the
// new listener can then fail to listen.
TEST_F(ListenerWriterTest,
       ListenFailureAfterAnActivationInsideTheListenerManagerCallFailsTheWrite) {
  createInitializedWriter();
  ON_CALL(listener_manager_, isWorkerStarted()).WillByDefault(Return(false));
  const uint64_t first_update_id = write("writer.a");
  activateInline();
  runApplyPass();

  const uint64_t second_update_id = write("writer.a", "v2", 10001);
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, "v2", true))
      .WillOnce(Invoke(
          [this](const ListenerProto& listener, const std::string&, bool) -> absl::StatusOr<bool> {
            activate(listener.name());
            writer_->onPostInit();
            ON_CALL(listener_manager_, isWorkerStarted()).WillByDefault(Return(true));
            writer_->onListenerListenFailure(listener.name(), /*removed=*/true);
            writer_->onListenerRemoval(listener.name());
            return true;
          }));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(first_update_id, Superseded),
                                                        IsOutcome(second_update_id, Failed)));
  workers_started_callback_->invokeCallback();
  EXPECT_TRUE(deliveredOutcomes().empty());
  EXPECT_EQ(0, gauge("listeners_owned"));
  EXPECT_EQ(0, counter("ownership_lost"));
}

TEST_F(ListenerWriterTest, FailedInPlaceUpdateInsideTheListenerManagerCallKeepsTheListener) {
  createInitializedWriter();
  write("writer.a");
  activateInline();
  runApplyPass();
  deliveredOutcomes();

  const uint64_t update_id = write("writer.a", "v2", 10001);
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, "v2", true))
      .WillOnce(Invoke(
          [this](const ListenerProto& listener, const std::string&, bool) -> absl::StatusOr<bool> {
            writer_->onListenerListenFailure(listener.name(), /*removed=*/false);
            return true;
          }));
  runApplyPass();
  const std::vector<Outcome> outcomes = deliveredOutcomes();
  ASSERT_EQ(1, outcomes.size());
  EXPECT_THAT(outcomes[0], IsOutcome(update_id, Failed));
  EXPECT_EQ("listener writer.a failed to listen and its active version keeps serving",
            outcomes[0].error_details);
  EXPECT_EQ(1, gauge("listeners_owned"));

  // A later write does not inherit the failure.
  const uint64_t next_update_id = write("writer.a", "v3", 10002);
  activateInline();
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(next_update_id, Active)));

  // The extension still owns the active listener.
  EXPECT_EQ(RemovalSuccess, writer_->removeListener("writer.a"));
  EXPECT_CALL(listener_manager_, removeListener("writer.a"));
  runApplyPass();
}

TEST_F(ListenerWriterTest, ListenFailureAfterWarmingFailsTheWarmingWrite) {
  createInitializedWriter();
  const uint64_t update_id = write("writer.a");
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, "v1", true)).WillOnce(Return(true));
  runApplyPass();
  // Failures of listeners without a warming write of the extension are ignored.
  writer_->onListenerListenFailure("writer.b", /*removed=*/true);

  writer_->onListenerListenFailure("writer.a", /*removed=*/true);
  writer_->onListenerRemoval("writer.a");
  const std::vector<Outcome> outcomes = deliveredOutcomes();
  ASSERT_EQ(1, outcomes.size());
  EXPECT_THAT(outcomes[0], IsOutcome(update_id, Failed));
  EXPECT_EQ("listener writer.a failed to listen and was removed", outcomes[0].error_details);
  EXPECT_EQ(0, counter("ownership_lost"));
  EXPECT_EQ(0, gauge("listeners_owned"));
}

// A listen failure of an owned listener without a warming write of the extension comes from
// another component, so the removal that follows ends the ownership.
TEST_F(ListenerWriterTest, ListenFailureWithoutAWarmingWriteLosesOwnershipOnTheRemoval) {
  createInitializedWriter();
  write("writer.a");
  activateInline();
  runApplyPass();
  deliveredOutcomes();

  writer_->onListenerListenFailure("writer.a", /*removed=*/true);
  writer_->onListenerRemoval("writer.a");
  EXPECT_TRUE(deliveredOutcomes().empty());
  EXPECT_EQ(1, counter("ownership_lost"));
  EXPECT_EQ(0, gauge("listeners_owned"));
}

TEST_F(ListenerWriterTest, FailedInPlaceUpdateAfterWarmingKeepsTheListener) {
  createInitializedWriter();
  write("writer.a");
  activateInline();
  runApplyPass();
  deliveredOutcomes();

  const uint64_t update_id = write("writer.a", "v2", 10001);
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, "v2", true)).WillOnce(Return(true));
  runApplyPass();
  writer_->onListenerListenFailure("writer.a", /*removed=*/false);
  const std::vector<Outcome> outcomes = deliveredOutcomes();
  ASSERT_EQ(1, outcomes.size());
  EXPECT_THAT(outcomes[0], IsOutcome(update_id, Failed));
  EXPECT_EQ("listener writer.a failed to listen and its active version keeps serving",
            outcomes[0].error_details);
  EXPECT_EQ(1, gauge("listeners_owned"));
}

TEST_F(ListenerWriterTest, ApplyPassPausesTheDependentXdsTypes) {
  createInitializedWriter();
  write("writer.a");
  bool paused = false;
  bool resumed = false;
  EXPECT_CALL(context_.xds_manager_,
              pause(Config::dependentTypeUrls(Config::getTypeUrl<ListenerProto>())))
      .WillOnce([&paused, &resumed](const std::vector<std::string>&) {
        paused = true;
        return std::make_unique<Cleanup>([&resumed]() { resumed = true; });
      });
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, "v1", true))
      .WillOnce(Invoke([&paused, &resumed](const ListenerProto&, const std::string&,
                                           bool) -> absl::StatusOr<bool> {
        EXPECT_TRUE(paused);
        EXPECT_FALSE(resumed);
        return true;
      }));
  runApplyPass();
  EXPECT_TRUE(resumed);
}

TEST_F(ListenerWriterTest, RemovesAnOwnedListener) {
  createInitializedWriter();
  write("writer.a");
  activateInline();
  runApplyPass();
  deliveredOutcomes();

  EXPECT_EQ(RemovalSuccess, writer_->removeListener("writer.a"));
  EXPECT_CALL(listener_manager_, removeListener("writer.a"))
      .WillOnce(Invoke([this](const std::string& listener_name) {
        writer_->onListenerRemoval(listener_name);
        return true;
      }));
  runApplyPass();
  EXPECT_EQ(0, counter("ownership_lost"));
  EXPECT_EQ(0, gauge("listeners_owned"));
  EXPECT_EQ(RemovalNotFound, writer_->removeListener("writer.a"));
}

TEST_F(ListenerWriterTest, RemovalCompletesTheWarmingWrite) {
  createInitializedWriter();
  const uint64_t update_id = write("writer.a");
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, "v1", true)).WillOnce(Return(true));
  runApplyPass();
  writer_->removeListener("writer.a");
  EXPECT_CALL(listener_manager_, removeListener("writer.a"));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(update_id, Removed)));
  EXPECT_EQ(1, counter("update_removed"));
}

TEST_F(ListenerWriterTest, RemovalIsSkippedAfterOwnershipIsLost) {
  createInitializedWriter();
  write("writer.a");
  activateInline();
  runApplyPass();
  deliveredOutcomes();

  writer_->removeListener("writer.a");
  activate("writer.a");
  runApplyPass();
  EXPECT_EQ(1, counter("ownership_lost"));
}

TEST_F(ListenerWriterTest, ChangesOfAListenerThatTheWriterDoesNotOwnYetAreIgnored) {
  createInitializedWriter();
  write("writer.a");
  // Another component adds and removes the listener before the write applies.
  activate("writer.a");
  writer_->onListenerRemoval("writer.a");
  EXPECT_EQ(0, counter("ownership_lost"));
  EXPECT_TRUE(deliveredOutcomes().empty());
}

TEST_F(ListenerWriterTest, OwnershipIsLostWhenAnotherComponentReplacesTheListener) {
  createInitializedWriter();
  write("writer.a");
  activateInline();
  runApplyPass();
  deliveredOutcomes();

  EXPECT_LOG_CONTAINS("warn", "listener writer.a was replaced by another component",
                      activate("writer.a"));
  EXPECT_TRUE(deliveredOutcomes().empty());
  EXPECT_EQ(1, counter("ownership_lost"));
  EXPECT_EQ(0, gauge("listeners_owned"));

  // Later writes of the listener fail.
  const uint64_t update_id = write("writer.a", "v2");
  listener_config_.name_ = "writer.a";
  EXPECT_CALL(listener_manager_,
              listeners(Server::ListenerManager::WARMING | Server::ListenerManager::ACTIVE))
      .WillOnce(Return(std::vector<std::reference_wrapper<Network::ListenerConfig>>{
          std::ref<Network::ListenerConfig>(listener_config_)}));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(update_id, Failed)));
}

TEST_F(ListenerWriterTest, OwnershipIsLostWhenAnotherComponentRemovesTheListener) {
  createInitializedWriter();
  const uint64_t update_id = write("writer.a");
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, "v1", true)).WillOnce(Return(true));
  runApplyPass();
  writer_->onListenerRemoval("writer.a");
  // Changes to listeners that the writer does not own are ignored.
  writer_->onListenerRemoval("writer.b");
  activate("writer.b");

  const std::vector<Outcome> outcomes = deliveredOutcomes();
  ASSERT_EQ(1, outcomes.size());
  EXPECT_THAT(outcomes[0], IsOutcome(update_id, OwnershipLost));
  EXPECT_EQ("listener writer.a was removed by another component", outcomes[0].error_details);
  EXPECT_EQ(1, counter("ownership_lost"));
  EXPECT_EQ(1, counter("update_ownership_lost"));
  EXPECT_EQ(RemovalNotFound, writer_->removeListener("writer.a"));
}

// A write that outlives the ownership of a removed listener does not match it.
TEST_F(ListenerWriterTest, WriteAfterAnotherComponentRemovedTheListenerDoesNotMatchIt) {
  createInitializedWriter();
  write("writer.a");
  activateInline();
  runApplyPass();
  deliveredOutcomes();

  const uint64_t update_id = write("writer.a", "v2");
  writer_->onListenerRemoval("writer.a");
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, "v2", true)).WillOnce(Return(false));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(update_id, Failed)));
}

TEST_F(ListenerWriterTest, OwnershipIsLostWhenAnotherComponentReplacesAWaitingListener) {
  createInitializedWriter();
  ON_CALL(listener_manager_, isWorkerStarted()).WillByDefault(Return(false));
  const uint64_t update_id = write("writer.a");
  activateInline();
  runApplyPass();

  activate("writer.a");
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(update_id, OwnershipLost)));
  EXPECT_EQ(0, gauge("listeners_owned"));
}

TEST_F(ListenerWriterTest, RemovalDuringTheFirstApplyRemovesTheListener) {
  createInitializedWriter();
  const uint64_t first_update_id = write("writer.a");
  uint64_t second_update_id = 0;
  EXPECT_CALL(listener_manager_, addOrUpdateListener(_, "v1", true))
      .WillOnce(Invoke(
          [&](const ListenerProto& listener, const std::string&, bool) -> absl::StatusOr<bool> {
            activate(listener.name());
            // A lifecycle hook of the module writes the listener again and removes it.
            second_update_id = write("writer.a", "v2");
            EXPECT_EQ(RemovalSuccess, writer_->removeListener("writer.a"));
            return true;
          }));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(second_update_id, Removed),
                                                        IsOutcome(first_update_id, Active)));

  EXPECT_CALL(listener_manager_, removeListener("writer.a"));
  runApplyPass();
  EXPECT_EQ(0, gauge("listeners_owned"));
}

// The writers of a server hold back the dependent xDS types of their resource types together, until
// none has a burst left or about one second after the first type was held back.
TEST(WriterSharedStateTest, WritersResumeTheDependentXdsTypesTogether) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  const std::shared_ptr<WriterSharedState> shared_state = WriterSharedState::get(context);
  const std::string cluster_type_url = "type.googleapis.com/envoy.config.cluster.v3.Cluster";
  const std::string listener_type_url = Config::getTypeUrl<ListenerProto>();
  const int cluster_writer = 0;
  const int listener_writer = 0;
  int cluster_type_resumes = 0;
  int listener_type_resumes = 0;
  EXPECT_CALL(context.xds_manager_, pause(Config::dependentTypeUrls(cluster_type_url)))
      .Times(2)
      .WillRepeatedly([&cluster_type_resumes](const std::vector<std::string>&) {
        return std::make_unique<Cleanup>([&cluster_type_resumes]() { ++cluster_type_resumes; });
      });
  EXPECT_CALL(context.xds_manager_, pause(Config::dependentTypeUrls(listener_type_url)))
      .Times(2)
      .WillRepeatedly([&listener_type_resumes](const std::vector<std::string>&) {
        return std::make_unique<Cleanup>([&listener_type_resumes]() { ++listener_type_resumes; });
      });

  const MonotonicTime start{};
  shared_state->pauseDependentTypes(cluster_type_url, context.xds_manager_, start);
  shared_state->onApplyPassDone(&cluster_writer, /*burst_applied=*/false, start);
  shared_state->pauseDependentTypes(listener_type_url, context.xds_manager_, start);
  shared_state->onApplyPassDone(&listener_writer, /*burst_applied=*/true, start);
  EXPECT_EQ(0, cluster_type_resumes);
  EXPECT_EQ(0, listener_type_resumes);
  shared_state->pauseDependentTypes(cluster_type_url, context.xds_manager_, start);
  shared_state->onApplyPassDone(&cluster_writer, /*burst_applied=*/true, start);
  EXPECT_EQ(1, cluster_type_resumes);
  EXPECT_EQ(1, listener_type_resumes);

  // A type held back later in the burst does not restart the time limit.
  shared_state->pauseDependentTypes(cluster_type_url, context.xds_manager_, start);
  shared_state->onApplyPassDone(&cluster_writer, /*burst_applied=*/false,
                                start + std::chrono::milliseconds(600));
  shared_state->pauseDependentTypes(listener_type_url, context.xds_manager_,
                                    start + std::chrono::milliseconds(600));
  shared_state->onApplyPassDone(&listener_writer, /*burst_applied=*/false,
                                start + std::chrono::milliseconds(600));
  EXPECT_EQ(1, cluster_type_resumes);
  shared_state->onApplyPassDone(&cluster_writer, /*burst_applied=*/false,
                                start + std::chrono::seconds(1));
  EXPECT_EQ(2, cluster_type_resumes);
  EXPECT_EQ(2, listener_type_resumes);
}

} // namespace
} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
