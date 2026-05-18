#include <chrono>

#include "envoy/grpc/async_client.h"
#include "envoy/registry/registry.h"

#include "source/common/grpc/common.h"
#include "source/common/protobuf/utility.h"

#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/thread_local_cluster.h"
#include "test/test_common/utility.h"

#include "contrib/reverse_tunnel_reporter/source/clients/grpc_client/client.h"
#include "contrib/reverse_tunnel_reporter/source/clients/grpc_client/factory.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class MockReverseTunnelReporter : public ReverseTunnelReporterWithState {
public:
  MOCK_METHOD(void, onServerInitialized, (), (override));
  MOCK_METHOD(void, reportConnectionEvent,
              (absl::string_view, absl::string_view, absl::string_view), (override));
  MOCK_METHOD(void, reportDisconnectionEvent, (absl::string_view, absl::string_view), (override));
  MOCK_METHOD(ReverseTunnelEvent::ConnectionsList, getAllConnections, (), (override));
};

GrpcConfigProto mockConfig() {
  GrpcConfigProto config_;

  config_.set_cluster("test_cluster");
  config_.mutable_default_send_interval()->set_seconds(2);
  config_.mutable_connect_retry_interval()->set_seconds(2);
  config_.set_max_retries(2);
  config_.set_max_buffer_count(1000);
  config_.set_stat_prefix("test.grpc_client");

  return config_;
}

ReverseTunnelEvent::ConnectionsList makeConnections(std::vector<std::string> node_ids) {
  ReverseTunnelEvent::ConnectionsList connections;
  std::string cluster = "test_cluster";
  std::string tenant = "test_tenant";

  for (const auto& node : node_ids) {
    auto conn = std::make_shared<ReverseTunnelEvent::Connected>(ReverseTunnelEvent::Connected{
        node, cluster, tenant, std::chrono::system_clock::time_point(std::chrono::seconds(1))});
    connections.push_back(std::move(conn));
  }

  return connections;
}

ReverseTunnelEvent::DisconnectionsList makeDisconnections(std::vector<std::string> node_ids) {
  std::string cluster = "test_cluster";
  ReverseTunnelEvent::DisconnectionsList disconnections;

  for (const auto& node : node_ids) {
    auto disconn = std::make_shared<ReverseTunnelEvent::Disconnected>(
        ReverseTunnelEvent::Disconnected{ReverseTunnelEvent::getName(node)});
    disconnections.push_back(std::move(disconn));
  }

  return disconnections;
}

StreamTunnelsResp validateReq(Buffer::InstancePtr& request,
                              const ReverseTunnelEvent::TunnelUpdates& actual, bool full_push) {
  StreamTunnelsReq req;
  bool success = Grpc::Common::parseBufferInstance(std::move(request), req);
  EXPECT_EQ(success, true);

  EXPECT_EQ(req.added_tunnels_size(), actual.connections.size());
  EXPECT_EQ(req.removed_tunnel_names_size(), actual.disconnections.size());

  for (std::size_t i = 0; i < actual.connections.size(); i++) {
    EXPECT_EQ(actual.connections[i]->node_id, req.added_tunnels(i).identity().node_id());
  }

  for (std::size_t i = 0; i < actual.disconnections.size(); i++) {
    EXPECT_EQ(actual.disconnections[i]->name, req.removed_tunnel_names(i));
  }

  EXPECT_EQ(full_push, req.full_push());

  StreamTunnelsResp resp;
  resp.set_request_nonce(req.nonce());

  return resp;
}

Protobuf::Duration getHalfDuration(const Protobuf::Duration& dur) {
  return Protobuf::util::TimeUtil::MillisecondsToDuration(
      DurationUtil::durationToMilliseconds(dur) / 2);
}

class GrpcClientTest : public testing::Test {
public:
  GrpcClientTest() = default;

  void SetUp() override {
    api_ = Api::createApiForTest(time_system_);
    dispatcher_ = api_->allocateDispatcher("test_thread");

    ON_CALL(context_, mainThreadDispatcher()).WillByDefault(ReturnRef(*dispatcher_));
    ON_CALL(context_, scope()).WillByDefault(ReturnRef(*stats_store_.rootScope()));
    ON_CALL(context_, clusterManager()).WillByDefault(ReturnRef(cm_));

    ON_CALL(context_, localInfo()).WillByDefault(ReturnRef(local_info_));
    ON_CALL(local_info_, nodeName()).WillByDefault(ReturnRef(node_id));
    ON_CALL(local_info_, clusterName()).WillByDefault(ReturnRef(cluster_id));

    ON_CALL(cm_, getThreadLocalCluster(_)).WillByDefault(Return(&thread_local_cluster_));
    ON_CALL(thread_local_cluster_, info()).WillByDefault(Return(cluster_info_));
    ON_CALL(*cluster_info_, statsScope()).WillByDefault(ReturnRef(*stats_store_.rootScope()));

    ON_CALL(cm_, grpcAsyncClientManager()).WillByDefault(ReturnRef(manager_));
    ON_CALL(manager_, getOrCreateRawAsyncClient(_, _, _))
        .WillByDefault(Return(absl::StatusOr<Grpc::RawAsyncClientSharedPtr>(async_client_)));
  }

protected:
  void incTime(const Protobuf::Duration& dur) {
    time_system_.advanceTimeAsyncImpl(
        std::chrono::milliseconds(DurationUtil::durationToMilliseconds(dur)));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  void getStream(int times) {
    EXPECT_CALL(*async_client_, startRaw(_, _, _, _))
        .Times(times)
        .WillRepeatedly(Invoke([this](absl::string_view, absl::string_view,
                                      Grpc::RawAsyncStreamCallbacks& callbacks,
                                      const Http::AsyncClient::StreamOptions&) {
          callbacks_ = &callbacks;
          return async_stream_.get();
        }));
  }

  GrpcClient::GrpcClientStats getStats() {
    return GrpcClient::GrpcClientStats{context_, config_.stat_prefix(), config_.cluster()};
  }

  std::shared_ptr<NiceMock<Grpc::MockAsyncClient>> async_client_{
      std::make_shared<NiceMock<Grpc::MockAsyncClient>>()};
  std::unique_ptr<NiceMock<Grpc::MockAsyncStream>> async_stream_{
      std::make_unique<NiceMock<Grpc::MockAsyncStream>>()};
  Grpc::RawAsyncStreamCallbacks* callbacks_;

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Grpc::MockAsyncClientManager> manager_;
  std::shared_ptr<const NiceMock<Upstream::MockClusterInfo>> cluster_info_{
      std::make_shared<const NiceMock<Upstream::MockClusterInfo>>()};

  Api::ApiPtr api_;
  Event::SimulatedTimeSystem time_system_;
  Event::DispatcherPtr dispatcher_;
  Stats::IsolatedStoreImpl stats_store_;

  std::string node_id{"tunnel-v2"};
  std::string cluster_id{"tunnel-v2"};

  GrpcConfigProto config_{mockConfig()};
  NiceMock<MockReverseTunnelReporter> mock_reporter_;
};

// Check the connection behaviour on server initialization (infinite retries)
TEST_F(GrpcClientTest, RetryAttemptsOnStreamCreationFailure) {
  GrpcClient client{context_, config_};
  auto stats_{getStats()};

  // The connection attempts shld not be bound by anything.
  // Making it config_.max_retries + 2 for a simple check of not bound by max_retries.
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _))
      .Times(config_.max_retries() + 2)
      .WillRepeatedly(Return(nullptr));

  client.onServerInitialized(&mock_reporter_);

  for (std::size_t i = 0; i < config_.max_retries() + 1; i++) {
    incTime(config_.connect_retry_interval());
  }

  // Not incremented because no connection attempt was successful.
  // startRaw => nullptr.
  EXPECT_EQ(stats_.connection_attempts_counter_.value(), 0);
  EXPECT_EQ(stats_.send_attempts_counter_.value(), 0);
  EXPECT_EQ(stats_.nonce_acked_gauge_.value(), 0);
  EXPECT_EQ(stats_.nonce_current_gauge_.value(), 0);
  EXPECT_EQ(stats_
                .getCounter(stats_.disconnects_,
                            stats_.getTags(
                                Grpc::Status::WellKnownGrpcStatus::Internal,
                                GrpcDisconnectionReason::DisconnectReason::STREAM_CREATION_FAILED))
                .value(),
            config_.max_retries() + 2);
}

// Checks the happy path -> server connect and full push.
TEST_F(GrpcClientTest, ClientSendsFullPushOnConnect) {
  GrpcClient client{context_, config_};
  auto stats_{getStats()};

  ReverseTunnelEvent::TunnelUpdates events{makeConnections({"node_1"}), {}};

  EXPECT_CALL(mock_reporter_, getAllConnections()).WillOnce(Return(events.connections));

  getStream(1);

  EXPECT_CALL(*async_stream_, sendMessageRaw_(_, false))
      .WillOnce(Invoke([&events](Buffer::InstancePtr& request, bool) {
        auto resp = validateReq(request, events, true);
        EXPECT_EQ(resp.request_nonce(), 1);
      }));

  client.onServerInitialized(&mock_reporter_);

  EXPECT_EQ(stats_.connection_attempts_counter_.value(), 1);
  EXPECT_EQ(stats_.acks_received_counter_.value(), 0);
  EXPECT_EQ(stats_.send_attempts_counter_.value(), 1);
  EXPECT_EQ(stats_.events_dropped_counter_.value(), 0);
  EXPECT_EQ(stats_.queued_updates_counter_.value(), 1);
  EXPECT_EQ(stats_.out_of_order_acks_counter_.value(), 0);
  EXPECT_EQ(stats_.nonce_current_gauge_.value(), 1);
  EXPECT_EQ(stats_.nonce_acked_gauge_.value(), 0);
  EXPECT_EQ(stats_.send_interval_gauge_.value(),
            DurationUtil::durationToMilliseconds(config_.default_send_interval()));
  EXPECT_EQ(stats_.sent_accepted_cnt_counter_.value(), 1);
  EXPECT_EQ(stats_.sent_removed_cnt_counter_.value(), 0);
}

// Checks the happy path -> Server up connect and send the diff after the full push
TEST_F(GrpcClientTest, ClientSendsDiffAfterFullPush) {
  GrpcClient client{context_, config_};
  auto stats_{getStats()};
  int cur = 0, total = 4;

  ReverseTunnelEvent::TunnelUpdates updates[] = {
      ReverseTunnelEvent::TunnelUpdates{makeConnections({"node_1"}), {}},
      ReverseTunnelEvent::TunnelUpdates{makeConnections({"node_2"}),
                                        makeDisconnections({"node_1"})},
      ReverseTunnelEvent::TunnelUpdates{makeConnections({"node_3", "node_4"}), {}},
      ReverseTunnelEvent::TunnelUpdates{makeConnections({"node_5"}),
                                        makeDisconnections({"node_3"})}};

  getStream(1);

  EXPECT_CALL(mock_reporter_, getAllConnections()).WillOnce(Invoke([&updates]() {
    return updates[0].connections;
  }));

  EXPECT_CALL(*async_stream_, sendMessageRaw_(_, false))
      .Times(total)
      .WillRepeatedly(Invoke([this, &updates, &cur](Buffer::InstancePtr& request, bool) {
        auto resp = validateReq(request, updates[cur], cur == 0);
        EXPECT_EQ(resp.request_nonce(), cur + 1);
        callbacks_->onReceiveMessageRaw(Grpc::Common::serializeMessage(resp));
      }));

  client.onServerInitialized(&mock_reporter_);
  cur++;

  for (; cur < total; cur++) {
    client.receiveEvents(updates[cur]);
    incTime(config_.default_send_interval());
  }

  int total_events = 0;
  for (int i = 0; i < total; i++) {
    total_events += updates[i].size();
  }

  EXPECT_EQ(stats_.connection_attempts_counter_.value(), 1);
  EXPECT_EQ(stats_.acks_received_counter_.value(), total);
  EXPECT_EQ(stats_.send_attempts_counter_.value(), total);
  EXPECT_EQ(stats_.sent_accepted_cnt_counter_.value(), 5);
  EXPECT_EQ(stats_.sent_removed_cnt_counter_.value(), 2);
  EXPECT_EQ(stats_.events_dropped_counter_.value(), 0);
  EXPECT_EQ(stats_.queued_updates_counter_.value(), total_events);
  EXPECT_EQ(stats_.out_of_order_acks_counter_.value(), 0);
  EXPECT_EQ(stats_.nonce_current_gauge_.value(), total);
  EXPECT_EQ(stats_.nonce_acked_gauge_.value(), total);
  EXPECT_EQ(stats_.send_interval_gauge_.value(),
            DurationUtil::durationToMilliseconds(config_.default_send_interval()));
}

// Check the happy path -> config changes from the server response is applied
TEST_F(GrpcClientTest, ReportIntervalChangesReflectInClient) {
  GrpcClient client{context_, config_};
  auto stats_{getStats()};
  int cur = 0;

  ReverseTunnelEvent::TunnelUpdates events;

  getStream(1);

  EXPECT_CALL(mock_reporter_, getAllConnections()).WillOnce(Invoke([&events]() {
    return events.connections;
  }));

  // This should be called 3 times.
  // Once for the first message and then twice for the next two increments.
  EXPECT_CALL(*async_stream_, sendMessageRaw_(_, false))
      .Times(3)
      .WillRepeatedly(Invoke([this, &events, &cur](Buffer::InstancePtr& request, bool) {
        auto resp = validateReq(request, events, cur == 0);
        EXPECT_EQ(resp.request_nonce(), ++cur);

        if (cur == 1) {
          *resp.mutable_report_interval() = getHalfDuration(config_.default_send_interval());
        }

        callbacks_->onReceiveMessageRaw(Grpc::Common::serializeMessage(resp));
      }));

  EXPECT_EQ(stats_.send_interval_gauge_.value(),
            DurationUtil::durationToMilliseconds(config_.default_send_interval()));
  client.onServerInitialized(&mock_reporter_);
  EXPECT_EQ(stats_.send_interval_gauge_.value(),
            DurationUtil::durationToMilliseconds(getHalfDuration(config_.default_send_interval())));

  // This is already scheduled from the next time we will use the half interval for sending.
  incTime(config_.default_send_interval());
  incTime(getHalfDuration(config_.default_send_interval()));

  EXPECT_EQ(stats_.send_attempts_counter_.value(), 3);
}

// Check edge case -> Full push and then diffs on reconnect
TEST_F(GrpcClientTest, FullPushAndDiffOnReconnect) {
  GrpcClient client{context_, config_};
  auto stats_{getStats()};
  int cur = 0, total = 4;

  ReverseTunnelEvent::TunnelUpdates updates[] = {
      ReverseTunnelEvent::TunnelUpdates{makeConnections({"node_1"}), {}},
      ReverseTunnelEvent::TunnelUpdates{makeConnections({"node_2"}),
                                        makeDisconnections({"node_1"})},
      ReverseTunnelEvent::TunnelUpdates{makeConnections({"node_3", "node_4"}), {}},
      ReverseTunnelEvent::TunnelUpdates{makeConnections({"node_5"}),
                                        makeDisconnections({"node_3"})}};

  // 2 stream creations: initial connect + reconnect after remote close.
  getStream(2);

  // getAllConnections is called once per full push (initial + reconnect).
  EXPECT_CALL(mock_reporter_, getAllConnections()).Times(2).WillRepeatedly(Invoke([&updates]() {
    return updates[0].connections;
  }));

  // total + 1: the reconnect triggers an extra full push on top of the normal total sends.
  EXPECT_CALL(*async_stream_, sendMessageRaw_(_, false))
      .Times(total + 1)
      .WillRepeatedly(Invoke([this, &updates, &cur](Buffer::InstancePtr& request, bool) {
        // cur is still 0 during both the initial connect and the reconnect full push,
        // so both correctly validate as full_push=true against updates[0].
        auto resp = validateReq(request, updates[cur], cur == 0);
        EXPECT_EQ(resp.request_nonce(), cur + 1);
        callbacks_->onReceiveMessageRaw(Grpc::Common::serializeMessage(resp));
      }));

  client.onServerInitialized(&mock_reporter_);

  // disconnect and reconnect -> sends the full push automatically
  callbacks_->onRemoteClose(Grpc::Status::WellKnownGrpcStatus::Unknown, "Testing");
  incTime(config_.connect_retry_interval());
  cur++; // cur becomes 1 only after the reconnect full push has already fired.

  for (; cur < total; cur++) {
    client.receiveEvents(updates[cur]);
    incTime(config_.default_send_interval());
  }

  int total_sz = 0;
  for (int i = 0; i < total; i++) {
    total_sz += updates[i].size();
  }

  EXPECT_EQ(stats_.connection_attempts_counter_.value(), 2);
  EXPECT_EQ(stats_.acks_received_counter_.value(), total + 1);
  EXPECT_EQ(stats_.send_attempts_counter_.value(), total + 1);
  // 6 = 1 (initial full push) + 1 (reconnect full push) + 1+2+1 from updates[1..3].
  EXPECT_EQ(stats_.sent_accepted_cnt_counter_.value(), 6);
  EXPECT_EQ(stats_.sent_removed_cnt_counter_.value(), 2);
  EXPECT_EQ(stats_.events_dropped_counter_.value(), 0);
  // +updates[0].size(): the reconnect full push re-queues the initial connections.
  EXPECT_EQ(stats_.queued_updates_counter_.value(), total_sz + updates[0].size());
  EXPECT_EQ(stats_.out_of_order_acks_counter_.value(), 0);
  EXPECT_EQ(stats_.nonce_current_gauge_.value(), total);
  EXPECT_EQ(stats_.nonce_acked_gauge_.value(), total);
  EXPECT_EQ(stats_
                .getCounter(stats_.disconnects_,
                            stats_.getTags(Grpc::Status::WellKnownGrpcStatus::Unknown,
                                           GrpcDisconnectionReason::DisconnectReason::REMOTE_CLOSE))
                .value(),
            1);
}

// Check edge case -> Disconnect on deadline exceeded
TEST_F(GrpcClientTest, DisconnectOnTooManyUnAckedRequests) {
  GrpcClient client{context_, config_};
  auto stats_{getStats()};
  std::size_t cur = 0;

  ReverseTunnelEvent::TunnelUpdates events;
  getStream(1);

  // max_retries + 1: the initial connect sends once, then max_retries timer ticks each send once.
  // On the next timer tick send() sees (nonce_current_ - nonce_acked_) > max_retries and
  // disconnects before calling sendMessage, so the total successful sends is max_retries + 1.
  EXPECT_CALL(*async_stream_, sendMessageRaw_(_, false))
      .Times(config_.max_retries() + 1)
      .WillRepeatedly(Invoke([&events, &cur](Buffer::InstancePtr& request, bool) {
        auto resp = validateReq(request, events, cur == 0);
        EXPECT_EQ(resp.request_nonce(), cur + 1);

        // No ACK is sent. It should eventually disconnect.
      }));

  EXPECT_CALL(*async_stream_, resetStream());

  client.onServerInitialized(&mock_reporter_);
  cur++;

  // max_retries + 2: we need max_retries timer ticks for the sends, plus one more tick
  // to trigger the disconnect check. The first send happens on connect (cur=0).
  for (; cur < config_.max_retries() + 2; cur++) {
    incTime(config_.default_send_interval());
  }

  EXPECT_EQ(stats_.connection_attempts_counter_.value(), 1);
  EXPECT_EQ(stats_.acks_received_counter_.value(), 0);
  EXPECT_EQ(stats_.send_attempts_counter_.value(), config_.max_retries() + 1);
  EXPECT_EQ(stats_.nonce_current_gauge_.value(), config_.max_retries() + 1);
  EXPECT_EQ(stats_.nonce_acked_gauge_.value(), 0);
  EXPECT_EQ(stats_
                .getCounter(
                    stats_.disconnects_,
                    stats_.getTags(Grpc::Status::WellKnownGrpcStatus::DeadlineExceeded,
                                   GrpcDisconnectionReason::DisconnectReason::MAX_RETRIES_EXCEEDED))
                .value(),
            1);
}

// Check edge case -> Disconnect with Server on NACK.
TEST_F(GrpcClientTest, DisconnectOnNack) {
  GrpcClient client{context_, config_};
  auto stats_{getStats()};

  ReverseTunnelEvent::TunnelUpdates events;
  getStream(1);

  EXPECT_CALL(*async_stream_, sendMessageRaw_(_, false))
      .WillOnce(Invoke([this, &events](Buffer::InstancePtr& request, bool) {
        auto resp = validateReq(request, events, true);
        EXPECT_EQ(resp.request_nonce(), 1);
        resp.mutable_error_detail()->set_code(Grpc::Status::WellKnownGrpcStatus::Unavailable);
        callbacks_->onReceiveMessageRaw(Grpc::Common::serializeMessage(resp));
      }));

  EXPECT_CALL(*async_stream_, resetStream());

  client.onServerInitialized(&mock_reporter_);

  EXPECT_EQ(stats_.connection_attempts_counter_.value(), 1);
  EXPECT_EQ(stats_.send_attempts_counter_.value(), 1);
  EXPECT_EQ(
      stats_
          .getCounter(stats_.disconnects_,
                      stats_.getTags(Grpc::Status::WellKnownGrpcStatus::Aborted,
                                     GrpcDisconnectionReason::DisconnectReason::NACK_RECEIVED))
          .value(),
      1);
}

// Check edge case -> Disconnect on Buffer Full (Also ensure that full push has no limits)
TEST_F(GrpcClientTest, DisconnectOnBufferFull) {
  GrpcClient client{context_, config_};
  auto stats_{getStats()};

  std::vector<std::string> nodes;
  for (std::size_t i = 0; i < config_.max_buffer_count() + 1; i++) {
    nodes.push_back("node_" + std::to_string(i));
  }

  ReverseTunnelEvent::TunnelUpdates connect_events{makeConnections(nodes), {}};
  getStream(1);

  EXPECT_CALL(mock_reporter_, getAllConnections()).WillOnce([&connect_events]() {
    return connect_events.connections;
  });

  EXPECT_CALL(*async_stream_, sendMessageRaw_(_, false))
      .WillOnce(Invoke([this, &connect_events](Buffer::InstancePtr& request, bool) {
        auto resp = validateReq(request, connect_events, true);
        EXPECT_EQ(resp.request_nonce(), 1);
        callbacks_->onReceiveMessageRaw(Grpc::Common::serializeMessage(resp));
      }));

  EXPECT_CALL(*async_stream_, resetStream());

  client.onServerInitialized(&mock_reporter_);
  client.receiveEvents(connect_events);

  EXPECT_EQ(stats_.connection_attempts_counter_.value(), 1);
  EXPECT_EQ(stats_.send_attempts_counter_.value(), 1);
  EXPECT_EQ(stats_.events_dropped_counter_.value(), nodes.size());
  EXPECT_EQ(stats_.sent_accepted_cnt_counter_.value(), nodes.size());
  EXPECT_EQ(stats_.sent_removed_cnt_counter_.value(), 0);
  EXPECT_EQ(stats_.queued_updates_counter_.value(), nodes.size());
  EXPECT_EQ(
      stats_
          .getCounter(stats_.disconnects_,
                      stats_.getTags(Grpc::Status::WellKnownGrpcStatus::ResourceExhausted,
                                     GrpcDisconnectionReason::DisconnectReason::BUFFER_OVERFLOW))
          .value(),
      1);
}

// Check edge case -> Prev Nonce ignored
TEST_F(GrpcClientTest, OutOfOrderNonce) {
  GrpcClient client{context_, config_};
  auto stats_{getStats()};
  std::size_t cur = 0;

  ReverseTunnelEvent::TunnelUpdates events;
  getStream(1);

  // Send nonce=0 (already acked) for all responses to trigger out-of-order.
  // nonce=0 is always <= nonce_acked_ (which starts at 0), so every response lands
  // in the else branch and increments out_of_order_acks_counter_.
  // Same +1/+2 arithmetic as DisconnectOnTooManyUnAckedRequests: max_retries + 1 sends
  // succeed before the disconnect fires.
  EXPECT_CALL(*async_stream_, sendMessageRaw_(_, false))
      .Times(config_.max_retries() + 1)
      .WillRepeatedly(Invoke([this, &events, &cur](Buffer::InstancePtr& request, bool) {
        auto resp = validateReq(request, events, cur == 0);
        EXPECT_EQ(resp.request_nonce(), cur + 1);

        resp.set_request_nonce(0);
        callbacks_->onReceiveMessageRaw(Grpc::Common::serializeMessage(resp));
      }));

  EXPECT_CALL(*async_stream_, resetStream());

  client.onServerInitialized(&mock_reporter_);
  cur++;

  for (; cur < config_.max_retries() + 2; cur++) {
    incTime(config_.default_send_interval());
  }

  EXPECT_EQ(stats_.connection_attempts_counter_.value(), 1);
  EXPECT_EQ(stats_.send_attempts_counter_.value(), config_.max_retries() + 1);
  EXPECT_EQ(stats_.out_of_order_acks_counter_.value(), config_.max_retries() + 1);
  EXPECT_EQ(stats_.nonce_current_gauge_.value(), config_.max_retries() + 1);
  EXPECT_EQ(stats_.nonce_acked_gauge_.value(), 0);
  EXPECT_EQ(stats_
                .getCounter(
                    stats_.disconnects_,
                    stats_.getTags(Grpc::Status::WellKnownGrpcStatus::DeadlineExceeded,
                                   GrpcDisconnectionReason::DisconnectReason::MAX_RETRIES_EXCEEDED))
                .value(),
            1);
}

// Check edge case -> Skip Nonce
TEST_F(GrpcClientTest, SkipNonce) {
  GrpcClient client{context_, config_};
  auto stats_{getStats()};
  std::size_t cur = 0;

  ReverseTunnelEvent::TunnelUpdates events;
  getStream(1);

  // max_retries + 2: the normal max_retries + 1 sends that would trigger disconnect,
  // but a late ACK at iteration max_retries advances nonce_acked_ and buys one more send.
  EXPECT_CALL(*async_stream_, sendMessageRaw_(_, false))
      .Times(config_.max_retries() + 2)
      .WillRepeatedly(Invoke([this, &events, &cur](Buffer::InstancePtr& request, bool) {
        auto resp = validateReq(request, events, cur == 0);
        EXPECT_EQ(resp.request_nonce(), cur + 1);

        // Only ACK the nonce at iteration max_retries, proving a single late ACK
        // advances the watermark and prevents disconnect.
        if (cur == config_.max_retries()) {
          callbacks_->onReceiveMessageRaw(Grpc::Common::serializeMessage(resp));
        }
      }));

  client.onServerInitialized(&mock_reporter_);
  cur++;

  for (; cur < config_.max_retries() + 2; cur++) {
    incTime(config_.default_send_interval());
  }

  // After the stream is up, retroactively send ACKs for earlier nonces.
  // These are all below nonce_acked_ now, so they count as out-of-order.
  for (std::size_t i = 1; i <= config_.max_retries(); i++) {
    StreamTunnelsResp resp;
    resp.set_request_nonce(i);
    callbacks_->onReceiveMessageRaw(Grpc::Common::serializeMessage(resp));
  }

  EXPECT_EQ(stats_.connection_attempts_counter_.value(), 1);
  EXPECT_EQ(stats_.send_attempts_counter_.value(), config_.max_retries() + 2);
  EXPECT_EQ(stats_.out_of_order_acks_counter_.value(), config_.max_retries());
  EXPECT_EQ(stats_.acks_received_counter_.value(), 1);
  EXPECT_EQ(stats_.nonce_current_gauge_.value(), config_.max_retries() + 2);
  EXPECT_EQ(stats_.nonce_acked_gauge_.value(), config_.max_retries() + 1);
}

// Edge case -> Remote close Status Ok
TEST_F(GrpcClientTest, OkRemoteClose) {
  GrpcClient client{context_, config_};
  auto stats_{getStats()};

  ReverseTunnelEvent::TunnelUpdates events;
  getStream(1);

  EXPECT_CALL(*async_stream_, sendMessageRaw_(_, false))
      .WillOnce(Invoke([&events](Buffer::InstancePtr& request, bool) {
        auto response = validateReq(request, events, true);
        EXPECT_EQ(response.request_nonce(), 1);
      }));

  EXPECT_CALL(*async_stream_, resetStream());

  client.onServerInitialized(&mock_reporter_);
  callbacks_->onRemoteClose(Grpc::Status::WellKnownGrpcStatus::Ok, "Testing");

  EXPECT_EQ(stats_.connection_attempts_counter_.value(), 1);
  EXPECT_EQ(stats_.send_attempts_counter_.value(), 1);
  EXPECT_EQ(stats_
                .getCounter(stats_.disconnects_,
                            stats_.getTags(Grpc::Status::WellKnownGrpcStatus::Ok,
                                           GrpcDisconnectionReason::DisconnectReason::REMOTE_CLOSE))
                .value(),
            1);
}

// --- Hardening tests ---

TEST_F(GrpcClientTest, ReceiveEventsBeforeInitialized) {
  GrpcClient client{context_, config_};
  auto stats_{getStats()};

  ReverseTunnelEvent::TunnelUpdates events{makeConnections({"node_1"}), {}};
  client.receiveEvents(std::move(events));

  EXPECT_EQ(stats_.queued_updates_counter_.value(), 0);
  EXPECT_EQ(stats_.events_dropped_counter_.value(), 0);
}

TEST_F(GrpcClientTest, ClusterNotFoundLogsAndReturns) {
  GrpcClient client{context_, config_};

  EXPECT_CALL(cm_, getThreadLocalCluster(_)).WillOnce(Return(nullptr));

  client.onServerInitialized(&mock_reporter_);

  ReverseTunnelEvent::TunnelUpdates events{makeConnections({"node_1"}), {}};
  client.receiveEvents(std::move(events));

  auto stats_{getStats()};
  EXPECT_EQ(stats_.connection_attempts_counter_.value(), 0);
  EXPECT_EQ(stats_.queued_updates_counter_.value(), 0);
}

TEST_F(GrpcClientTest, ClientCreationFailureLogsAndReturns) {
  GrpcClient client{context_, config_};

  EXPECT_CALL(manager_, getOrCreateRawAsyncClient(_, _, _))
      .WillOnce(Return(absl::InvalidArgumentError("Bad Karma")));

  client.onServerInitialized(&mock_reporter_);

  ReverseTunnelEvent::TunnelUpdates events{makeConnections({"node_1"}), {}};
  client.receiveEvents(std::move(events));

  auto stats_{getStats()};
  EXPECT_EQ(stats_.connection_attempts_counter_.value(), 0);
  EXPECT_EQ(stats_.queued_updates_counter_.value(), 0);
}

TEST_F(GrpcClientTest, BufferOverflowWhileDisconnectedDoesNotRearmRetry) {
  GrpcClient client{context_, config_};
  auto stats_{getStats()};

  std::vector<std::string> nodes;
  for (std::size_t i = 0; i < config_.max_buffer_count() + 1; i++) {
    nodes.push_back("node_" + std::to_string(i));
  }

  ReverseTunnelEvent::TunnelUpdates big_update{makeConnections(nodes), {}};
  getStream(1);

  EXPECT_CALL(mock_reporter_, getAllConnections()).WillOnce(Return(big_update.connections));

  EXPECT_CALL(*async_stream_, sendMessageRaw_(_, false))
      .WillOnce(Invoke([this, &big_update](Buffer::InstancePtr& request, bool) {
        auto resp = validateReq(request, big_update, true);
        callbacks_->onReceiveMessageRaw(Grpc::Common::serializeMessage(resp));
      }));

  EXPECT_CALL(*async_stream_, resetStream());

  client.onServerInitialized(&mock_reporter_);

  // First overflow: stream is alive, should disconnect.
  client.receiveEvents(ReverseTunnelEvent::TunnelUpdates{makeConnections(nodes), {}});
  EXPECT_EQ(
      stats_
          .getCounter(stats_.disconnects_,
                      stats_.getTags(Grpc::Status::WellKnownGrpcStatus::ResourceExhausted,
                                     GrpcDisconnectionReason::DisconnectReason::BUFFER_OVERFLOW))
          .value(),
      1);

  // Second overflow: stream is null, should NOT increment disconnect counter.
  client.receiveEvents(ReverseTunnelEvent::TunnelUpdates{makeConnections(nodes), {}});
  EXPECT_EQ(
      stats_
          .getCounter(stats_.disconnects_,
                      stats_.getTags(Grpc::Status::WellKnownGrpcStatus::ResourceExhausted,
                                     GrpcDisconnectionReason::DisconnectReason::BUFFER_OVERFLOW))
          .value(),
      1);

  // Events still counted as dropped both times.
  EXPECT_EQ(stats_.events_dropped_counter_.value(), nodes.size() * 2);
}

// Verify default values when proto fields are unset/zero.
TEST_F(GrpcClientTest, ConfigDefaults) {
  GrpcConfigProto bare_config;
  bare_config.set_cluster("test_cluster");

  GrpcClientConfig parsed(bare_config);

  EXPECT_EQ(parsed.stat_prefix, "reverse_tunnel_reporter_client.grpc_client");
  EXPECT_EQ(parsed.cluster, "test_cluster");
  EXPECT_EQ(parsed.send_interval.count(), 5000);
  EXPECT_EQ(parsed.connect_retry_interval.count(), 5000);
  EXPECT_EQ(parsed.max_retries, 5);
  EXPECT_EQ(parsed.max_buffer, 1000000);
}

class GrpcClientFactoryTest : public testing::Test {
public:
  void SetUp() override {
    factory_ = Registry::FactoryRegistry<ReverseTunnelReporterClientFactory>::getFactory(
        "envoy.extensions.reverse_tunnel.reverse_tunnel_reporting_service.clients.grpc_client");
    ASSERT_NE(nullptr, factory_);

    ON_CALL(context_, messageValidationVisitor())
        .WillByDefault(ReturnRef(ProtobufMessage::getStrictValidationVisitor()));
  }

protected:
  ReverseTunnelReporterClientFactory* factory_{};
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
};

TEST_F(GrpcClientFactoryTest, Name) {
  EXPECT_EQ("envoy.extensions.reverse_tunnel.reverse_tunnel_reporting_service.clients.grpc_client",
            factory_->name());
}

TEST_F(GrpcClientFactoryTest, CreateEmptyConfigProto) {
  auto config = factory_->createEmptyConfigProto();
  EXPECT_NE(nullptr, config);
  EXPECT_NE(nullptr, dynamic_cast<envoy::extensions::reverse_tunnel_reporters::v3alpha::clients::
                                      grpc_client::GrpcClientConfig*>(config.get()));
}

TEST_F(GrpcClientFactoryTest, CreateClientReturnsNonNull) {
  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr dispatcher = api->allocateDispatcher("test");
  ON_CALL(context_, mainThreadDispatcher()).WillByDefault(ReturnRef(*dispatcher));
  Stats::IsolatedStoreImpl stats_store;
  ON_CALL(context_, scope()).WillByDefault(ReturnRef(*stats_store.rootScope()));

  GrpcConfigProto config;
  config.set_cluster("test_cluster");

  auto client = factory_->createClient(context_, config);
  EXPECT_NE(nullptr, client);
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
