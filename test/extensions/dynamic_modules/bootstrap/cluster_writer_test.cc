#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.validate.h"

#include "source/common/common/cleanup.h"
#include "source/common/config/dependent_type_urls.h"
#include "source/common/config/resource_name.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stats/utility.h"
#include "source/extensions/bootstrap/dynamic_modules/cluster_writer.h"
#include "source/extensions/bootstrap/dynamic_modules/writer_shared_state.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/logging.h"
#include "test/test_common/simulated_time_system.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {
namespace {

using testing::_;
using testing::HasSubstr;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Not;
using testing::Return;

constexpr auto Success = envoy_dynamic_module_type_bootstrap_cluster_writer_update_result_Success;
constexpr auto Active = envoy_dynamic_module_type_bootstrap_cluster_writer_update_status_Active;
constexpr auto Superseded =
    envoy_dynamic_module_type_bootstrap_cluster_writer_update_status_Superseded;
constexpr auto Removed = envoy_dynamic_module_type_bootstrap_cluster_writer_update_status_Removed;
constexpr auto Failed = envoy_dynamic_module_type_bootstrap_cluster_writer_update_status_Failed;
constexpr auto OwnershipLost =
    envoy_dynamic_module_type_bootstrap_cluster_writer_update_status_OwnershipLost;

struct Outcome {
  uint64_t update_id;
  std::string cluster_name;
  envoy_dynamic_module_type_bootstrap_cluster_writer_update_status status;
  std::string error_details;
};

std::string serialize(const envoy::config::cluster::v3::Cluster& cluster) {
  std::string serialized;
  EXPECT_TRUE(cluster.SerializeToString(&serialized));
  return serialized;
}

std::string serializedCluster(absl::string_view name) {
  envoy::config::cluster::v3::Cluster cluster;
  cluster.set_name(name);
  cluster.mutable_connect_timeout()->set_seconds(1);
  return serialize(cluster);
}

class ClusterWriterTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  ClusterWriterTest() {
    ON_CALL(context_.init_manager_, add(_))
        .WillByDefault(Invoke([this](const Init::Target& target) {
          init_target_handle_ = target.createHandle("test");
        }));
  }

  void createWriter(bool with_done_hook = true, absl::string_view extension_name = "writer") {
    DynamicModuleClusterWriter::UpdateDoneCallback update_done;
    if (with_done_hook) {
      update_done = [this](uint64_t update_id, absl::string_view cluster_name,
                           envoy_dynamic_module_type_bootstrap_cluster_writer_update_status status,
                           absl::string_view error_details) {
        outcomes_.push_back(
            {update_id, std::string(cluster_name), status, std::string(error_details)});
        if (on_outcome_ != nullptr) {
          on_outcome_();
        }
      };
    }
    // The writer creates its apply callback first, and the newest expectation matches first.
    deliver_callback_ = new NiceMock<Event::MockSchedulableCallback>(&context_.dispatcher_);
    apply_callback_ = new NiceMock<Event::MockSchedulableCallback>(&context_.dispatcher_);
    writer_ = std::make_unique<DynamicModuleClusterWriter>(extension_name, context_,
                                                           std::move(update_done));
  }

  // Creates a writer that accepts writes and applies them.
  void createInitializedWriter() {
    createWriter();
    writer_->onServerInitialized();
    init_target_handle_->initialize(init_watcher_);
  }

  uint64_t write(absl::string_view cluster_name, absl::string_view version_info = "v1") {
    uint64_t update_id = 0;
    EXPECT_EQ(Success, writer_->addOrUpdateCluster(cluster_name, serializedCluster(cluster_name),
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

  void activate(absl::string_view cluster_name) {
    Upstream::ThreadLocalClusterCommand command;
    writer_->onClusterAddOrUpdate(cluster_name, command);
  }

  uint64_t counter(absl::string_view name, absl::string_view config_name = "writer") {
    Stats::StatNameDynamicPool pool(context_.store_.symbolTable());
    const Stats::StatNameTagVector tags{{pool.add("config_name"), pool.add(config_name)}};
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
    return {Stats::DynamicName("dynamic_modules"), Stats::DynamicName("cluster_writer"),
            Stats::DynamicName(name)};
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<Init::ExpectableWatcherImpl> init_watcher_;
  Init::TargetHandlePtr init_target_handle_;
  Event::MockSchedulableCallback* apply_callback_{};
  Event::MockSchedulableCallback* deliver_callback_{};
  std::unique_ptr<DynamicModuleClusterWriter> writer_;
  std::vector<Outcome> outcomes_;
  // Runs in the done hook after each outcome.
  std::function<void()> on_outcome_;
};

MATCHER_P2(IsOutcome, update_id, status, "") {
  return arg.update_id == update_id && arg.status == status;
}

TEST_F(ClusterWriterTest, RejectsWritesBeforeServerInitialized) {
  createWriter();
  uint64_t update_id = 1;
  EXPECT_EQ(
      envoy_dynamic_module_type_bootstrap_cluster_writer_update_result_NotAllowed,
      writer_->addOrUpdateCluster("writer.a", serializedCluster("writer.a"), "v1", update_id));
  EXPECT_EQ(0, update_id);
  EXPECT_EQ(envoy_dynamic_module_type_bootstrap_cluster_writer_removal_result_NotAllowed,
            writer_->removeCluster("writer.a"));
  EXPECT_EQ(1, counter("update_rejected"));
}

TEST_F(ClusterWriterTest, StatsOfAnUnnamedExtensionUseTheDefaultConfigName) {
  createWriter(/*with_done_hook=*/true, /*extension_name=*/"");
  uint64_t update_id = 0;
  EXPECT_EQ(
      envoy_dynamic_module_type_bootstrap_cluster_writer_update_result_NotAllowed,
      writer_->addOrUpdateCluster("writer.a", serializedCluster("writer.a"), "v1", update_id));
  EXPECT_EQ(1, counter("update_rejected", "default"));
}

TEST_F(ClusterWriterTest, ShutdownRefusesWritesAndDropsQueuedWork) {
  createInitializedWriter();
  write("writer.b");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v1", true)).WillOnce(Return(false));
  runApplyPass();
  EXPECT_EQ(1, gauge("clusters_owned"));
  write("writer.a");
  // The second write leaves an undelivered Superseded outcome.
  write("writer.a", "v2");
  EXPECT_CALL(*apply_callback_, cancel());
  EXPECT_CALL(*deliver_callback_, cancel());
  writer_->onShutdown();
  EXPECT_EQ(0, gauge("clusters_owned"));
  EXPECT_EQ(0, gauge("updates_pending"));

  uint64_t update_id = 0;
  EXPECT_EQ(
      envoy_dynamic_module_type_bootstrap_cluster_writer_update_result_NotAllowed,
      writer_->addOrUpdateCluster("writer.a", serializedCluster("writer.a"), "v1", update_id));
  EXPECT_EQ(envoy_dynamic_module_type_bootstrap_cluster_writer_removal_result_NotAllowed,
            writer_->removeCluster("writer.a"));

  // Callbacks that fire anyway find no work.
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, _, _)).Times(0);
  apply_callback_->enabled_ = true;
  runApplyPass();
  deliver_callback_->enabled_ = true;
  EXPECT_TRUE(deliveredOutcomes().empty());
}

TEST_F(ClusterWriterTest, AddsItsInitTargetWhenTheServerIsInitialized) {
  createWriter();
  EXPECT_EQ(nullptr, init_target_handle_);
  writer_->onServerInitialized();
  ASSERT_NE(nullptr, init_target_handle_);
  EXPECT_CALL(context_.cluster_manager_, addThreadLocalClusterUpdateCallbacks_(_));
  EXPECT_CALL(init_watcher_, ready());
  init_target_handle_->initialize(init_watcher_);
}

TEST_F(ClusterWriterTest, StartsWithoutAnInitTargetOnceTheServerInitManagerStarted) {
  ON_CALL(context_.init_manager_, state()).WillByDefault(Return(Init::Manager::State::Initialized));
  EXPECT_CALL(context_.init_manager_, add(_)).Times(0);
  createWriter();
  EXPECT_CALL(context_.cluster_manager_, addThreadLocalClusterUpdateCallbacks_(_));
  writer_->onServerInitialized();
  const uint64_t update_id = write("writer.a");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v1", true)).WillOnce(Return(false));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(update_id, Active)));
}

TEST_F(ClusterWriterTest, ShutdownBeforeClusterInitializationSkipsTheClusterManager) {
  createWriter();
  writer_->onServerInitialized();
  writer_->onShutdown();
  EXPECT_CALL(context_.cluster_manager_, addThreadLocalClusterUpdateCallbacks_(_)).Times(0);
  EXPECT_CALL(init_watcher_, ready());
  init_target_handle_->initialize(init_watcher_);
}

TEST_F(ClusterWriterTest, OutcomesAreDeliveredInALaterLoopIteration) {
  createInitializedWriter();
  const uint64_t update_id = write("writer.a");
  EXPECT_CALL(*deliver_callback_, scheduleCallbackCurrentIteration()).Times(0);
  EXPECT_CALL(*deliver_callback_, scheduleCallbackNextIteration());
  write("writer.a", "v2");
  EXPECT_TRUE(outcomes_.empty());
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(update_id, Superseded)));
}

TEST_F(ClusterWriterTest, WritesWaitForClusterInitialization) {
  createWriter();
  EXPECT_CALL(context_.cluster_manager_, addThreadLocalClusterUpdateCallbacks_(_)).Times(0);
  writer_->onServerInitialized();
  write("writer.a");
  EXPECT_FALSE(apply_callback_->enabled_);

  EXPECT_CALL(context_.cluster_manager_, addThreadLocalClusterUpdateCallbacks_(_));
  EXPECT_CALL(*apply_callback_, scheduleCallbackNextIteration());
  EXPECT_CALL(init_watcher_, ready());
  init_target_handle_->initialize(init_watcher_);
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v1", true)).WillOnce(Return(true));
  runApplyPass();
}

TEST_F(ClusterWriterTest, WriteIsActiveOnceTheClusterWarms) {
  createInitializedWriter();
  const uint64_t update_id = write("writer.a");
  EXPECT_EQ(1, update_id);
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v1", true))
      .WillOnce(Invoke([](const envoy::config::cluster::v3::Cluster& cluster, absl::string_view,
                          bool) -> absl::StatusOr<bool> {
        EXPECT_EQ("writer.a", cluster.name());
        return true;
      }));
  runApplyPass();
  EXPECT_TRUE(deliveredOutcomes().empty());
  EXPECT_EQ(1, gauge("updates_pending"));
  EXPECT_EQ(1, gauge("clusters_owned"));

  activate("writer.a");
  const std::vector<Outcome> outcomes = deliveredOutcomes();
  ASSERT_EQ(1, outcomes.size());
  EXPECT_THAT(outcomes[0], IsOutcome(update_id, Active));
  EXPECT_EQ("writer.a", outcomes[0].cluster_name);
  EXPECT_EQ("", outcomes[0].error_details);
  EXPECT_EQ(1, counter("update_submitted"));
  EXPECT_EQ(1, counter("update_active"));
  EXPECT_EQ(0, gauge("updates_pending"));
}

TEST_F(ClusterWriterTest, WriteIsActiveWhenTheClusterWarmsInline) {
  createInitializedWriter();
  const uint64_t update_id = write("writer.a");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v1", true))
      .WillOnce(Invoke([this](const envoy::config::cluster::v3::Cluster& cluster, absl::string_view,
                              bool) -> absl::StatusOr<bool> {
        activate(cluster.name());
        return true;
      }));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(update_id, Active)));

  // The next write that does not activate inline waits for the cluster to warm.
  const uint64_t second_update_id = write("writer.a", "v2");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v2", true)).WillOnce(Return(true));
  runApplyPass();
  EXPECT_TRUE(deliveredOutcomes().empty());
  activate("writer.a");
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(second_update_id, Active)));
}

TEST_F(ClusterWriterTest, UpdateOfAnOwnedClusterAppliesTheLatestWrite) {
  createInitializedWriter();
  write("writer.a");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v1", true)).WillOnce(Return(false));
  runApplyPass();
  deliveredOutcomes();
  context_.cluster_manager_.initializeClusters({"writer.a"}, {});

  envoy::config::cluster::v3::Cluster cluster;
  cluster.set_name("writer.a");
  cluster.mutable_connect_timeout()->set_seconds(2);
  uint64_t first_update_id = 0;
  EXPECT_EQ(Success,
            writer_->addOrUpdateCluster("writer.a", serialize(cluster), "v2", first_update_id));
  cluster.mutable_connect_timeout()->set_seconds(3);
  uint64_t second_update_id = 0;
  EXPECT_EQ(Success,
            writer_->addOrUpdateCluster("writer.a", serialize(cluster), "v3", second_update_id));
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v3", true))
      .WillOnce(Invoke([](const envoy::config::cluster::v3::Cluster& cluster, absl::string_view,
                          bool) -> absl::StatusOr<bool> {
        EXPECT_EQ(3, cluster.connect_timeout().seconds());
        return false;
      }));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(first_update_id, Superseded),
                                                        IsOutcome(second_update_id, Active)));
}

TEST_F(ClusterWriterTest, UnchangedWriteIsActive) {
  createInitializedWriter();
  const uint64_t update_id = write("writer.a");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v1", true)).WillOnce(Return(false));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(update_id, Active)));
}

TEST_F(ClusterWriterTest, UnchangedWriteSupersedesTheWarmingWrite) {
  createInitializedWriter();
  const uint64_t first_update_id = write("writer.a");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, _, true))
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

TEST_F(ClusterWriterTest, QueuedWriteIsSupersededAndKeepsItsPlace) {
  createInitializedWriter();
  const uint64_t first_update_id = write("writer.a");
  write("writer.b");
  const uint64_t third_update_id = write("writer.a", "v2");
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(first_update_id, Superseded)));

  {
    InSequence sequence;
    EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v2", true))
        .WillOnce(Return(false));
    EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v1", true))
        .WillOnce(Return(false));
  }
  runApplyPass();
  const std::vector<Outcome> outcomes = deliveredOutcomes();
  ASSERT_EQ(2, outcomes.size());
  EXPECT_THAT(outcomes[0], IsOutcome(third_update_id, Active));
  EXPECT_EQ(1, counter("update_superseded"));
}

TEST_F(ClusterWriterTest, WarmingWriteIsSupersededByTheNextWrite) {
  createInitializedWriter();
  const uint64_t first_update_id = write("writer.a");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, _, true))
      .WillRepeatedly(Return(true));
  runApplyPass();
  const uint64_t second_update_id = write("writer.a", "v2");
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(first_update_id, Superseded)));

  activate("writer.a");
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(second_update_id, Active)));
}

TEST_F(ClusterWriterTest, InvalidWritesFail) {
  createInitializedWriter();
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, _, _)).Times(0);
  uint64_t update_id = 0;

  // The cluster does not parse.
  EXPECT_EQ(Success, writer_->addOrUpdateCluster("writer.a",
                                                 std::string("\x0a\x05"
                                                             "abc",
                                                             5),
                                                 "v1", update_id));
  // The cluster has a different name.
  EXPECT_EQ(Success, writer_->addOrUpdateCluster("writer.b", serializedCluster("writer.c"), "v1",
                                                 update_id));
  // The cluster has an invalid duration.
  envoy::config::cluster::v3::Cluster cluster;
  cluster.set_name("writer.c");
  cluster.mutable_connect_timeout()->set_seconds(-1);
  EXPECT_EQ(Success, writer_->addOrUpdateCluster("writer.c", serialize(cluster), "v1", update_id));
  // The cluster breaks a constraint.
  cluster.mutable_connect_timeout()->set_seconds(1);
  cluster.set_name("writer.d");
  cluster.set_lb_policy(static_cast<envoy::config::cluster::v3::Cluster::LbPolicy>(1000));
  EXPECT_EQ(Success, writer_->addOrUpdateCluster("writer.d", serialize(cluster), "v1", update_id));
  runApplyPass();

  const std::vector<Outcome> outcomes = deliveredOutcomes();
  ASSERT_EQ(4, outcomes.size());
  for (const Outcome& outcome : outcomes) {
    EXPECT_EQ(Failed, outcome.status);
  }
  EXPECT_EQ("failed to parse cluster writer.a", outcomes[0].error_details);
  EXPECT_EQ("cluster writer.b is named writer.c in its configuration", outcomes[1].error_details);
  EXPECT_THAT(outcomes[2].error_details, HasSubstr("invalid cluster writer.c"));
  EXPECT_THAT(outcomes[3].error_details, HasSubstr("invalid cluster writer.d"));
  // The errors do not carry the contents of the cluster.
  EXPECT_THAT(outcomes[3].error_details, Not(HasSubstr("connect_timeout")));
  EXPECT_EQ(4, counter("update_failed"));
  EXPECT_EQ(0, gauge("clusters_owned"));
}

// The writer accepts and rejects the same clusters as MessageUtil::validate, which CDS uses.
TEST_F(ClusterWriterTest, ValidationMatchesMessageUtilValidate) {
  createInitializedWriter();
  ON_CALL(context_.cluster_manager_, addOrUpdateCluster(_, _, _)).WillByDefault(Return(false));
  std::vector<envoy::config::cluster::v3::Cluster> clusters(5);
  clusters[0].mutable_connect_timeout()->set_seconds(1);
  clusters[1].mutable_connect_timeout()->set_nanos(-1);
  clusters[2].set_lb_policy(static_cast<envoy::config::cluster::v3::Cluster::LbPolicy>(1000));
  clusters[3].mutable_per_connection_buffer_limit_bytes()->set_value(1);
  // Only the duration check rejects this one, since it passes the constraints of the proto.
  clusters[4].mutable_connect_timeout()->set_seconds(315576000001);
  for (size_t i = 0; i < clusters.size(); i++) {
    const std::string cluster_name = absl::StrCat("writer.", i);
    clusters[i].set_name(cluster_name);
    bool accepted_by_message_util = true;
    try {
      MessageUtil::validate(clusters[i],
                            context_.messageValidationContext().dynamicValidationVisitor());
    } catch (const EnvoyException&) {
      accepted_by_message_util = false;
    }
    uint64_t update_id = 0;
    EXPECT_EQ(Success,
              writer_->addOrUpdateCluster(cluster_name, serialize(clusters[i]), "v1", update_id));
    runApplyPass();
    const std::vector<Outcome> outcomes = deliveredOutcomes();
    ASSERT_EQ(1, outcomes.size());
    EXPECT_EQ(accepted_by_message_util, outcomes[0].status != Failed) << cluster_name;
  }
}

TEST_F(ClusterWriterTest, WriteFailsWhenAnotherComponentOwnsTheName) {
  createInitializedWriter();
  const uint64_t update_id = write("writer.a");
  EXPECT_CALL(context_.cluster_manager_, hasCluster(absl::string_view("writer.a")))
      .WillOnce(Return(true));
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, _, _)).Times(0);
  runApplyPass();
  const std::vector<Outcome> outcomes = deliveredOutcomes();
  ASSERT_EQ(1, outcomes.size());
  EXPECT_THAT(outcomes[0], IsOutcome(update_id, Failed));
  EXPECT_EQ("cluster writer.a exists and is not owned by this extension",
            outcomes[0].error_details);
}

TEST_F(ClusterWriterTest, WriteFailsWhenTheClusterManagerRejectsIt) {
  createInitializedWriter();
  const uint64_t update_id = write("writer.a");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v1", true))
      .WillOnce(Return(absl::InvalidArgumentError("contents of the cluster")));
  runApplyPass();
  const std::vector<Outcome> outcomes = deliveredOutcomes();
  ASSERT_EQ(1, outcomes.size());
  EXPECT_THAT(outcomes[0], IsOutcome(update_id, Failed));
  EXPECT_EQ("the cluster manager rejected cluster writer.a", outcomes[0].error_details);
  EXPECT_EQ(1, counter("update_failed"));
  EXPECT_EQ(0, gauge("clusters_owned"));
  EXPECT_EQ(envoy_dynamic_module_type_bootstrap_cluster_writer_removal_result_NotFound,
            writer_->removeCluster("writer.a"));
}

TEST_F(ClusterWriterTest, WriteThatThrowsAfterReplacingTheClusterKeepsIt) {
  createInitializedWriter();
  const uint64_t update_id = write("writer.a");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v1", true))
      .WillOnce(Invoke(
          [](const envoy::config::cluster::v3::Cluster&, absl::string_view,
             bool) -> absl::StatusOr<bool> { throw EnvoyException("load balancer failure"); }));
  EXPECT_CALL(context_.cluster_manager_, hasCluster(absl::string_view("writer.a")))
      .WillOnce(Return(false))
      .WillOnce(Return(true));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(update_id, Failed)));
  EXPECT_EQ(1, gauge("clusters_owned"));

  // The writer still owns the cluster, so it can remove it.
  EXPECT_EQ(envoy_dynamic_module_type_bootstrap_cluster_writer_removal_result_Success,
            writer_->removeCluster("writer.a"));
  EXPECT_CALL(context_.cluster_manager_, removeCluster(absl::string_view("writer.a"), true));
  runApplyPass();
}

TEST_F(ClusterWriterTest, WriteThatThrowsAfterActivatingTheClusterSupersedesTheWarmingWrite) {
  createInitializedWriter();
  const uint64_t first_update_id = write("writer.a");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v1", true)).WillOnce(Return(true));
  runApplyPass();
  const uint64_t second_update_id = write("writer.a", "v2");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v2", true))
      .WillOnce(Invoke([this](const envoy::config::cluster::v3::Cluster& cluster, absl::string_view,
                              bool) -> absl::StatusOr<bool> {
        // The cluster manager activates the new cluster before its load balancer fails.
        context_.cluster_manager_.initializeClusters({cluster.name()}, {});
        throw EnvoyException("load balancer failure");
      }));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(first_update_id, Superseded),
                                                        IsOutcome(second_update_id, Failed)));
  EXPECT_EQ(0, gauge("updates_pending"));
  EXPECT_EQ(1, gauge("clusters_owned"));
}

TEST_F(ClusterWriterTest, UnchangedWriteOfAClusterThatFailedToInitializeFails) {
  createInitializedWriter();
  const uint64_t first_update_id = write("writer.a");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v1", true))
      .WillOnce(Invoke([this](const envoy::config::cluster::v3::Cluster& cluster, absl::string_view,
                              bool) -> absl::StatusOr<bool> {
        // The cluster manager activates the new cluster before its load balancer fails.
        context_.cluster_manager_.initializeClusters({cluster.name()}, {});
        throw EnvoyException("load balancer failure");
      }));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(first_update_id, Failed)));

  // The cluster manager finds the same write unchanged, but the workers do not use the cluster.
  const uint64_t second_update_id = write("writer.a", "v2");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v2", true)).WillOnce(Return(false));
  runApplyPass();
  const std::vector<Outcome> outcomes = deliveredOutcomes();
  ASSERT_EQ(1, outcomes.size());
  EXPECT_THAT(outcomes[0], IsOutcome(second_update_id, Failed));
  EXPECT_EQ("the cluster manager failed to initialize cluster writer.a", outcomes[0].error_details);

  // A write that the cluster manager applies replaces that cluster.
  const uint64_t third_update_id = write("writer.a", "v3");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v3", true)).WillOnce(Return(true));
  runApplyPass();
  activate("writer.a");
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(third_update_id, Active)));
  const uint64_t fourth_update_id = write("writer.a", "v4");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v4", true)).WillOnce(Return(false));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(fourth_update_id, Active)));
}

TEST_F(ClusterWriterTest, ApplyPassPausesTheDependentXdsTypes) {
  createInitializedWriter();
  write("writer.a");
  bool resumed = false;
  EXPECT_CALL(
      context_.xds_manager_,
      pause(Config::dependentTypeUrls(Config::getTypeUrl<envoy::config::cluster::v3::Cluster>())))
      .WillOnce([&resumed](const std::vector<std::string>&) {
        return std::make_unique<Cleanup>([&resumed]() { resumed = true; });
      });
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v1", true))
      .WillOnce(Invoke([&resumed](const envoy::config::cluster::v3::Cluster&, absl::string_view,
                                  bool) -> absl::StatusOr<bool> {
        EXPECT_FALSE(resumed);
        return false;
      }));
  runApplyPass();
  EXPECT_TRUE(resumed);
}

TEST_F(ClusterWriterTest, PassesOfABurstShareOneDependentXdsPause) {
  createInitializedWriter();
  write("writer.a");
  write("writer.b");
  int resumes = 0;
  EXPECT_CALL(
      context_.xds_manager_,
      pause(Config::dependentTypeUrls(Config::getTypeUrl<envoy::config::cluster::v3::Cluster>())))
      .WillOnce([&resumes](const std::vector<std::string>&) {
        return std::make_unique<Cleanup>([&resumes]() { ++resumes; });
      });
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, _, true))
      .WillOnce(Invoke([this](const envoy::config::cluster::v3::Cluster&, absl::string_view,
                              bool) -> absl::StatusOr<bool> {
        simTime().advanceTimeWait(std::chrono::milliseconds(10));
        return false;
      }))
      .WillOnce(Return(false));
  runApplyPass();
  EXPECT_EQ(0, resumes);
  runApplyPass();
  EXPECT_EQ(1, resumes);
}

TEST_F(ClusterWriterTest, DependentXdsPauseEndsAfterASecondAndAtShutdown) {
  createInitializedWriter();
  write("writer.a");
  write("writer.b");
  write("writer.c");
  int resumes = 0;
  EXPECT_CALL(
      context_.xds_manager_,
      pause(Config::dependentTypeUrls(Config::getTypeUrl<envoy::config::cluster::v3::Cluster>())))
      .Times(2)
      .WillRepeatedly([&resumes](const std::vector<std::string>&) {
        return std::make_unique<Cleanup>([&resumes]() { ++resumes; });
      });
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, _, true))
      .WillOnce(Invoke([this](const envoy::config::cluster::v3::Cluster&, absl::string_view,
                              bool) -> absl::StatusOr<bool> {
        simTime().advanceTimeWait(std::chrono::seconds(1));
        return false;
      }))
      .WillOnce(Invoke([this](const envoy::config::cluster::v3::Cluster&, absl::string_view,
                              bool) -> absl::StatusOr<bool> {
        simTime().advanceTimeWait(std::chrono::milliseconds(10));
        return false;
      }));
  runApplyPass();
  EXPECT_EQ(1, resumes);
  runApplyPass();
  EXPECT_EQ(1, resumes);
  writer_->onShutdown();
  EXPECT_EQ(2, resumes);
}

TEST_F(ClusterWriterTest, WritersShareOneDependentXdsPause) {
  createInitializedWriter();
  // The writer creates its apply callback first, and the newest expectation matches first.
  new NiceMock<Event::MockSchedulableCallback>(&context_.dispatcher_);
  auto* second_apply_callback = new NiceMock<Event::MockSchedulableCallback>(&context_.dispatcher_);
  DynamicModuleClusterWriter second_writer("second", context_, nullptr);
  second_writer.onServerInitialized();
  init_target_handle_->initialize(init_watcher_);
  write("writer.a");
  write("writer.b");
  uint64_t update_id = 0;
  EXPECT_EQ(Success, second_writer.addOrUpdateCluster("second.a", serializedCluster("second.a"),
                                                      "v1", update_id));
  int resumes = 0;
  EXPECT_CALL(
      context_.xds_manager_,
      pause(Config::dependentTypeUrls(Config::getTypeUrl<envoy::config::cluster::v3::Cluster>())))
      .WillOnce([&resumes](const std::vector<std::string>&) {
        return std::make_unique<Cleanup>([&resumes]() { ++resumes; });
      });
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, _, true))
      .WillOnce(Invoke([this](const envoy::config::cluster::v3::Cluster&, absl::string_view,
                              bool) -> absl::StatusOr<bool> {
        simTime().advanceTimeWait(std::chrono::milliseconds(10));
        return false;
      }))
      .WillRepeatedly(Return(false));
  runApplyPass();
  second_apply_callback->invokeCallback();
  EXPECT_EQ(0, resumes);
  // The pause outlives a writer that shuts down while another still applies its burst.
  second_writer.onShutdown();
  EXPECT_EQ(0, resumes);
  runApplyPass();
  EXPECT_EQ(1, resumes);
}

TEST_F(ClusterWriterTest, RemovesAnOwnedCluster) {
  createInitializedWriter();
  write("writer.a");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v1", true)).WillOnce(Return(false));
  runApplyPass();
  deliveredOutcomes();

  EXPECT_EQ(envoy_dynamic_module_type_bootstrap_cluster_writer_removal_result_Success,
            writer_->removeCluster("writer.a"));
  EXPECT_CALL(context_.cluster_manager_, removeCluster(absl::string_view("writer.a"), true))
      .WillOnce(Invoke([this](absl::string_view cluster_name, bool) {
        writer_->onClusterRemoval(cluster_name);
        return true;
      }));
  runApplyPass();
  EXPECT_EQ(0, counter("ownership_lost"));
  EXPECT_EQ(0, gauge("clusters_owned"));
  EXPECT_EQ(envoy_dynamic_module_type_bootstrap_cluster_writer_removal_result_NotFound,
            writer_->removeCluster("writer.a"));
}

TEST_F(ClusterWriterTest, RemovalCompletesTheWarmingWrite) {
  createInitializedWriter();
  const uint64_t update_id = write("writer.a");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v1", true)).WillOnce(Return(true));
  runApplyPass();
  writer_->removeCluster("writer.a");
  EXPECT_CALL(context_.cluster_manager_, removeCluster(absl::string_view("writer.a"), true));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(update_id, Removed)));
  EXPECT_EQ(1, counter("update_removed"));
}

TEST_F(ClusterWriterTest, RemovalCancelsAQueuedWrite) {
  createInitializedWriter();
  const uint64_t update_id = write("writer.a");
  EXPECT_EQ(envoy_dynamic_module_type_bootstrap_cluster_writer_removal_result_Success,
            writer_->removeCluster("writer.a"));
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(update_id, Removed)));
  EXPECT_EQ(envoy_dynamic_module_type_bootstrap_cluster_writer_removal_result_NotFound,
            writer_->removeCluster("writer.a"));
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, _, _)).Times(0);
  runApplyPass();
}

TEST_F(ClusterWriterTest, QueuedRemovalOfAnOwnedClusterReplacesItsQueuedWrite) {
  createInitializedWriter();
  write("writer.a");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v1", true)).WillOnce(Return(false));
  runApplyPass();
  deliveredOutcomes();

  const uint64_t update_id = write("writer.a", "v2");
  writer_->removeCluster("writer.a");
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(update_id, Removed)));
  EXPECT_CALL(context_.cluster_manager_, removeCluster(absl::string_view("writer.a"), true));
  runApplyPass();
}

TEST_F(ClusterWriterTest, SecondRemovalOfAQueuedRemovalHasNoOutcome) {
  createInitializedWriter();
  write("writer.a");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v1", true)).WillOnce(Return(false));
  runApplyPass();
  deliveredOutcomes();

  EXPECT_EQ(envoy_dynamic_module_type_bootstrap_cluster_writer_removal_result_Success,
            writer_->removeCluster("writer.a"));
  EXPECT_EQ(envoy_dynamic_module_type_bootstrap_cluster_writer_removal_result_Success,
            writer_->removeCluster("writer.a"));
  EXPECT_TRUE(deliveredOutcomes().empty());
  EXPECT_EQ(0, gauge("updates_pending"));
  EXPECT_CALL(context_.cluster_manager_, removeCluster(absl::string_view("writer.a"), true));
  runApplyPass();
}

TEST_F(ClusterWriterTest, WriteReplacesAQueuedRemoval) {
  createInitializedWriter();
  write("writer.a");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, _, true))
      .WillRepeatedly(Return(false));
  runApplyPass();
  deliveredOutcomes();

  writer_->removeCluster("writer.a");
  const uint64_t update_id = write("writer.a", "v2");
  EXPECT_CALL(context_.cluster_manager_, removeCluster(_, _)).Times(0);
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(update_id, Active)));
}

TEST_F(ClusterWriterTest, RemovalIsSkippedAfterOwnershipIsLost) {
  createInitializedWriter();
  write("writer.a");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v1", true)).WillOnce(Return(false));
  runApplyPass();
  deliveredOutcomes();

  writer_->removeCluster("writer.a");
  activate("writer.a");
  EXPECT_CALL(context_.cluster_manager_, removeCluster(_, _)).Times(0);
  runApplyPass();
  EXPECT_EQ(1, counter("ownership_lost"));
}

TEST_F(ClusterWriterTest, OwnershipIsLostWhenAnotherComponentReplacesTheCluster) {
  createInitializedWriter();
  write("writer.a");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v1", true)).WillOnce(Return(false));
  runApplyPass();
  deliveredOutcomes();

  EXPECT_LOG_CONTAINS("warn", "cluster writer.a was replaced by another component",
                      activate("writer.a"));
  EXPECT_TRUE(deliveredOutcomes().empty());
  EXPECT_EQ(1, counter("ownership_lost"));
  EXPECT_EQ(0, gauge("clusters_owned"));

  // Later writes of the cluster fail.
  const uint64_t update_id = write("writer.a", "v2");
  EXPECT_CALL(context_.cluster_manager_, hasCluster(absl::string_view("writer.a")))
      .WillOnce(Return(true));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(update_id, Failed)));
}

TEST_F(ClusterWriterTest, OwnershipIsLostWhenAnotherComponentRemovesTheCluster) {
  createInitializedWriter();
  const uint64_t update_id = write("writer.a");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v1", true)).WillOnce(Return(true));
  runApplyPass();
  writer_->onClusterRemoval("writer.a");
  // Changes to clusters that the writer does not own are ignored.
  writer_->onClusterRemoval("writer.b");
  activate("writer.b");

  const std::vector<Outcome> outcomes = deliveredOutcomes();
  ASSERT_EQ(1, outcomes.size());
  EXPECT_THAT(outcomes[0], IsOutcome(update_id, OwnershipLost));
  EXPECT_EQ("cluster writer.a was removed by another component", outcomes[0].error_details);
  EXPECT_EQ(1, counter("ownership_lost"));
  EXPECT_EQ(1, counter("update_ownership_lost"));
  EXPECT_EQ(envoy_dynamic_module_type_bootstrap_cluster_writer_removal_result_NotFound,
            writer_->removeCluster("writer.a"));
}

TEST_F(ClusterWriterTest, ApplyPassYieldsOnceItsBudgetIsSpent) {
  createInitializedWriter();
  write("writer.a");
  write("writer.b");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, _, true))
      .WillOnce(Invoke([this](const envoy::config::cluster::v3::Cluster& cluster, absl::string_view,
                              bool) -> absl::StatusOr<bool> {
        EXPECT_EQ("writer.a", cluster.name());
        simTime().advanceTimeWait(std::chrono::milliseconds(10));
        return false;
      }));
  EXPECT_CALL(*apply_callback_, scheduleCallbackNextIteration());
  runApplyPass();

  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, _, true))
      .WillOnce(Invoke([](const envoy::config::cluster::v3::Cluster& cluster, absl::string_view,
                          bool) -> absl::StatusOr<bool> {
        EXPECT_EQ("writer.b", cluster.name());
        return false;
      }));
  runApplyPass();
  EXPECT_EQ(2, deliveredOutcomes().size());
}

TEST_F(ClusterWriterTest, WritesQueuedDuringAPassWaitForTheNextPass) {
  createInitializedWriter();
  write("writer.a");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v1", true))
      .WillOnce(Invoke([this](const envoy::config::cluster::v3::Cluster&, absl::string_view,
                              bool) -> absl::StatusOr<bool> {
        write("writer.b", "v2");
        return false;
      }));
  EXPECT_CALL(*apply_callback_, scheduleCallbackCurrentIteration()).Times(0);
  EXPECT_CALL(*apply_callback_, scheduleCallbackNextIteration());
  runApplyPass();

  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v2", true)).WillOnce(Return(false));
  runApplyPass();
}

TEST_F(ClusterWriterTest, RewriteDuringAPassWaitsForTheNextPass) {
  createInitializedWriter();
  write("writer.a");
  write("writer.b");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v1", true))
      .WillOnce(Invoke([this](const envoy::config::cluster::v3::Cluster& cluster, absl::string_view,
                              bool) -> absl::StatusOr<bool> {
        EXPECT_EQ("writer.a", cluster.name());
        write("writer.b", "v2");
        return false;
      }));
  runApplyPass();

  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v2", true)).WillOnce(Return(false));
  runApplyPass();
}

TEST_F(ClusterWriterTest, RemovalDuringAPassWaitsForTheNextPass) {
  createInitializedWriter();
  write("writer.b");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v1", true)).WillOnce(Return(false));
  runApplyPass();
  write("writer.a", "v2");
  const uint64_t update_id = write("writer.b", "v2");
  deliveredOutcomes();

  EXPECT_CALL(context_.cluster_manager_, removeCluster(_, _)).Times(0);
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v2", true))
      .WillOnce(Invoke([this](const envoy::config::cluster::v3::Cluster& cluster, absl::string_view,
                              bool) -> absl::StatusOr<bool> {
        EXPECT_EQ("writer.a", cluster.name());
        EXPECT_EQ(envoy_dynamic_module_type_bootstrap_cluster_writer_removal_result_Success,
                  writer_->removeCluster("writer.b"));
        return false;
      }));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::Contains(IsOutcome(update_id, Removed)));

  EXPECT_CALL(context_.cluster_manager_, removeCluster(absl::string_view("writer.b"), true));
  runApplyPass();
}

TEST_F(ClusterWriterTest, RemovalDuringTheFirstApplyRemovesTheCluster) {
  createInitializedWriter();
  const uint64_t first_update_id = write("writer.a");
  uint64_t second_update_id = 0;
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v1", true))
      .WillOnce(Invoke([&](const envoy::config::cluster::v3::Cluster& cluster, absl::string_view,
                           bool) -> absl::StatusOr<bool> {
        activate(cluster.name());
        // A lifecycle hook of the module writes the cluster again and removes it.
        second_update_id = write("writer.a", "v2");
        EXPECT_EQ(envoy_dynamic_module_type_bootstrap_cluster_writer_removal_result_Success,
                  writer_->removeCluster("writer.a"));
        return true;
      }));
  runApplyPass();
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(second_update_id, Removed),
                                                        IsOutcome(first_update_id, Active)));

  EXPECT_CALL(context_.cluster_manager_, removeCluster(absl::string_view("writer.a"), true));
  runApplyPass();
  EXPECT_EQ(0, gauge("clusters_owned"));
}

// A write from the done hook that supersedes a queued write reports it in the next delivery.
TEST_F(ClusterWriterTest, OutcomeOfAWriteFromTheDoneHookArrivesInTheNextDelivery) {
  createInitializedWriter();
  const uint64_t first_update_id = write("writer.a");
  const uint64_t second_update_id = write("writer.a", "v2");
  bool wrote_again = false;
  on_outcome_ = [this, &wrote_again]() {
    if (!wrote_again) {
      wrote_again = true;
      write("writer.a", "v3");
    }
  };
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(first_update_id, Superseded)));
  EXPECT_THAT(deliveredOutcomes(), testing::ElementsAre(IsOutcome(second_update_id, Superseded)));
}

TEST_F(ClusterWriterTest, WritesAreAppliedWithoutADoneHook) {
  createWriter(/*with_done_hook=*/false);
  writer_->onServerInitialized();
  init_target_handle_->initialize(init_watcher_);
  write("writer.a");
  EXPECT_CALL(context_.cluster_manager_, addOrUpdateCluster(_, "v1", true)).WillOnce(Return(false));
  runApplyPass();
  EXPECT_FALSE(deliver_callback_->enabled_);
  EXPECT_EQ(1, counter("update_active"));
}

// A burst that lasts past the time limit resumes the dependent xDS types anyway, counted from its
// first pass.
TEST(WriterSharedStateTest, BurstResumesTheDependentXdsTypesASecondAfterItsFirstPass) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  const std::shared_ptr<WriterSharedState> shared_state = WriterSharedState::get(context);
  EXPECT_EQ(shared_state, WriterSharedState::get(context));
  const std::string type_url = Config::getTypeUrl<envoy::config::cluster::v3::Cluster>();
  const int writer = 0;
  int resumes = 0;
  EXPECT_CALL(context.xds_manager_, pause(Config::dependentTypeUrls(type_url)))
      .WillOnce([&resumes](const std::vector<std::string>&) {
        return std::make_unique<Cleanup>([&resumes]() { ++resumes; });
      });

  const MonotonicTime start{};
  shared_state->pauseDependentTypes(type_url, context.xds_manager_, start);
  shared_state->onApplyPassDone(&writer, /*burst_applied=*/false,
                                start + std::chrono::milliseconds(600));
  shared_state->pauseDependentTypes(type_url, context.xds_manager_,
                                    start + std::chrono::milliseconds(600));
  EXPECT_EQ(0, resumes);
  shared_state->onApplyPassDone(&writer, /*burst_applied=*/false, start + std::chrono::seconds(1));
  EXPECT_EQ(1, resumes);
}

} // namespace
} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
