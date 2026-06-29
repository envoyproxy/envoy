#include "contrib/reverse_tunnel_reporter/source/clients/grpc_client/client.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

GrpcClientConfig::GrpcClientConfig(const GrpcConfigProto& proto_config)
    : stat_prefix(PROTOBUF_GET_STRING_OR_DEFAULT(proto_config, stat_prefix,
                                                 "reverse_tunnel_reporter_client.grpc_client")),
      cluster(proto_config.cluster()),
      send_interval(PROTOBUF_GET_MS_OR_DEFAULT(proto_config, default_send_interval, 5000)),
      connect_retry_interval(
          PROTOBUF_GET_MS_OR_DEFAULT(proto_config, connect_retry_interval, 5000)),
      max_retries(proto_config.max_retries() ? proto_config.max_retries() : 5),
      max_buffer(proto_config.max_buffer_count() ? proto_config.max_buffer_count() : 1000000) {}

GrpcClient::GrpcClient(Server::Configuration::ServerFactoryContext& context,
                       const GrpcConfigProto& config)
    : context_{context}, config_{config},
      service_method_{*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "envoy.extensions.reverse_tunnel_reporters.v3alpha.clients.grpc_client"
          ".ReverseTunnelReportingService.StreamReverseTunnels")},
      stats_(context_, config_.stat_prefix, config_.cluster) {
  ENVOY_LOG(info,
            "GrpcClient: constructed: cluster={}, send_interval={}ms, retry_interval={}ms, "
            "max_retries={}, max_buffer={}",
            config_.cluster, config_.send_interval.count(), config_.connect_retry_interval.count(),
            config_.max_retries, config_.max_buffer);

  send_timer_ = context.mainThreadDispatcher().createTimer([this]() { send(false); });

  retry_timer_ = context.mainThreadDispatcher().createTimer([this]() { connect(); });

  stats_.send_interval_gauge_.set(config_.send_interval.count());
}

void GrpcClient::onServerInitialized(ReverseTunnelReporterWithState* reporter) {
  reporter_ = reporter;

  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name(config_.cluster);

  auto thread_local_cluster = context_.clusterManager().getThreadLocalCluster(config_.cluster);
  if (!thread_local_cluster) {
    ENVOY_LOG(error, "GrpcClient: cluster '{}' not found, cannot initialize", config_.cluster);
    return;
  }

  auto result = context_.clusterManager().grpcAsyncClientManager().getOrCreateRawAsyncClient(
      grpc_service, thread_local_cluster->info()->statsScope(), false);
  if (!result.ok()) {
    ENVOY_LOG(error, "GrpcClient: failed to create gRPC async client: {}",
              result.status().message());
    return;
  }

  async_client_ = Grpc::AsyncClient<StreamTunnelsReq, StreamTunnelsResp>(result.value());
  ENVOY_LOG(info, "GrpcClient: initialized: cluster={}", config_.cluster);

  initialized_ = true;
  connect();
}

void GrpcClient::receiveEvents(ReverseTunnelEvent::TunnelUpdates updates) {
  // Either we errored out of the initialized -> prevent infinite growth.
  // Or the onServerInitialized has not been called yet -> no worries we will do a full push on
  // connect.
  if (!initialized_) {
    ENVOY_LOG(debug, "GrpcClient: not initialized, cannot receive events");
    return;
  }

  if ((updates.size() + queued_updates_.size()) > config_.max_buffer) {
    ENVOY_LOG(error,
              "GrpcClient: buffer overflow: cluster={}, queued={}, incoming={}, max_buffer={}",
              config_.cluster, queued_updates_.size(), updates.size(), config_.max_buffer);

    stats_.events_dropped_counter_.add(updates.size());

    // Only disconnect if the stream is alive. If already disconnected, calling disconnect()
    // would re-arm the retry timer, delaying the reconnect that is already scheduled.
    if (stream_ != nullptr) {
      stats_
          .getCounter(stats_.disconnects_,
                      stats_.getTags(Grpc::Status::WellKnownGrpcStatus::ResourceExhausted,
                                     GrpcDisconnectionReason::DisconnectReason::BUFFER_OVERFLOW))
          .inc();
      disconnect();
    }
    return;
  }

  const auto incoming_conns = updates.connections.size();
  const auto incoming_disconns = updates.disconnections.size();
  stats_.queued_updates_counter_.add(updates.size());
  queued_updates_ += std::move(updates);
  ENVOY_LOG(debug, "GrpcClient: enqueued: cluster={}, +={}, -={}, queued_now={}", config_.cluster,
            incoming_conns, incoming_disconns, queued_updates_.size());
}

void GrpcClient::onReceiveMessage(Grpc::ResponsePtr<StreamTunnelsResp>&& message) {
  const auto resp_nonce = message->request_nonce();

  if (message->has_error_detail()) {
    ENVOY_LOG(error, "GrpcClient: NACK: cluster={}, nonce={}", config_.cluster, resp_nonce);

    stats_
        .getCounter(stats_.disconnects_,
                    stats_.getTags(Grpc::Status::WellKnownGrpcStatus::Aborted,
                                   GrpcDisconnectionReason::DisconnectReason::NACK_RECEIVED))
        .inc();
    return disconnect();
  }

  // A server cannot ACK a nonce we never sent. If this fires the server has a bug.
  ASSERT(
      resp_nonce <= nonce_current_,
      fmt::format("server acked nonce {} but we only sent up to {}", resp_nonce, nonce_current_));

  // Valid ACK: must be newer than the last acked watermark and within what we've sent.
  if (resp_nonce > nonce_acked_ && resp_nonce <= nonce_current_) {
    stats_.acks_received_counter_.inc();
    nonce_acked_ = resp_nonce;
    stats_.nonce_acked_gauge_.set(nonce_acked_);

    // The server may dynamically adjust our send cadence via report_interval in each ACK.
    // The new interval will take effect from the next schedule onwards the already scheduled send
    // is not affected. This is sticky and stays across disconnects also.
    config_.send_interval = std::chrono::milliseconds(
        PROTOBUF_GET_MS_OR_DEFAULT(*message, report_interval, config_.send_interval.count()));
    stats_.send_interval_gauge_.set(config_.send_interval.count());

    ENVOY_LOG(debug, "GrpcClient: ACK: cluster={}, nonce={}", config_.cluster, resp_nonce);
  } else {
    ENVOY_LOG(error, "GrpcClient: out-of-order ACK: cluster={}, nonce={}, expected_range=[{}, {}]",
              config_.cluster, resp_nonce, nonce_acked_ + 1, nonce_current_);
    stats_.out_of_order_acks_counter_.inc();
  }
}

void GrpcClient::onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) {
  // Even a graceful close is unexpected — the server should keep the stream open indefinitely.
  if (status == Grpc::Status::WellKnownGrpcStatus::Ok) {
    ENVOY_LOG(error, "GrpcClient: remote close (ok): cluster={}, message={}", config_.cluster,
              message);
  } else {
    ENVOY_LOG(error, "GrpcClient: remote close: cluster={}, status={}, message={}", config_.cluster,
              status, message);
  }

  stats_
      .getCounter(stats_.disconnects_,
                  stats_.getTags(status, GrpcDisconnectionReason::DisconnectReason::REMOTE_CLOSE))
      .inc();
  disconnect();
}

void GrpcClient::connect() {
  ENVOY_LOG(info, "GrpcClient: connecting: cluster={}", config_.cluster);

  stream_ = async_client_.start(service_method_, *this, Http::AsyncClient::StreamOptions());
  if (stream_ == nullptr) {
    ENVOY_LOG(error, "GrpcClient: stream creation failed: cluster={}", config_.cluster);

    stats_
        .getCounter(
            stats_.disconnects_,
            stats_.getTags(Grpc::Status::WellKnownGrpcStatus::Internal,
                           GrpcDisconnectionReason::DisconnectReason::STREAM_CREATION_FAILED))
        .inc();
    return disconnect();
  }

  // New stream, new nonce epoch. Stale nonces from the previous stream are meaningless.
  nonce_acked_ = nonce_current_ = 0;
  stats_.nonce_current_gauge_.set(0);
  stats_.nonce_acked_gauge_.set(0);

  stats_.connection_attempts_counter_.inc();
  ENVOY_LOG(info, "GrpcClient: connected: cluster={}", config_.cluster);
  send(true);
}

void GrpcClient::disconnect() {
  if (stream_ != nullptr) {
    stream_.resetStream();
    stream_ = nullptr;
  }

  // Stop the send loop — no point sending on a dead stream. The retry timer will reconnect.
  send_timer_->disableTimer();
  setTimer(retry_timer_, config_.connect_retry_interval);
  ENVOY_LOG(debug, "GrpcClient: disconnect, scheduled reconnect: cluster={}, retry_in_ms={}",
            config_.cluster, config_.connect_retry_interval.count());
}

void GrpcClient::send(bool full_push) {
  ASSERT(stream_ != nullptr);
  // Too many in-flight unacked messages — the server is likely dead or stuck. Disconnect
  // and let the retry timer establish a fresh stream.
  if ((nonce_current_ - nonce_acked_) > config_.max_retries) {
    ENVOY_LOG(error, "GrpcClient: too many unacked requests: cluster={}, nonce={}", config_.cluster,
              nonce_current_);

    stats_
        .getCounter(stats_.disconnects_,
                    stats_.getTags(Grpc::Status::WellKnownGrpcStatus::DeadlineExceeded,
                                   GrpcDisconnectionReason::DisconnectReason::MAX_RETRIES_EXCEEDED))
        .inc();
    return disconnect();
  }

  ENVOY_LOG(debug, "GrpcClient: sending: cluster={}, full_push={}, queued_now={}", config_.cluster,
            full_push, queued_updates_.size());
  stats_.send_attempts_counter_.inc();
  stream_.sendMessage(constructMessage(full_push), false);

  setTimer(send_timer_, config_.send_interval);
}

StreamTunnelsReq GrpcClient::constructMessage(bool full_push) {
  // Full push replaces the pending diff queue with a complete snapshot from the reporter.
  // Any queued diffs are stale at this point because the full snapshot supersedes them.
  if (full_push) {
    stats_.events_dropped_counter_.add(queued_updates_.size());
    queued_updates_ = ReverseTunnelEvent::TunnelUpdates{{reporter_->getAllConnections()}, {}};
    stats_.queued_updates_counter_.add(queued_updates_.size());
    ENVOY_LOG(info, "GrpcClient: full_push queued: cluster={}, queued_now={}", config_.cluster,
              queued_updates_.size());
  }

  StreamTunnelsReq message;

  auto* node = message.mutable_node();
  node->set_id(context_.localInfo().nodeName());
  node->set_cluster(context_.localInfo().clusterName());

  auto* added_tunnels = message.mutable_added_tunnels();
  for (auto& conn : queued_updates_.connections) {
    auto* new_tunnel = added_tunnels->Add();
    new_tunnel->set_name(ReverseTunnelEvent::getName(conn->node_id));
    TimestampUtil::systemClockToTimestamp(conn->created_at, *new_tunnel->mutable_created_at());

    auto* tunnel_id = new_tunnel->mutable_identity();
    tunnel_id->set_tenant_id(conn->tenant_id);
    tunnel_id->set_cluster_id(conn->cluster_id);
    tunnel_id->set_node_id(conn->node_id);
  }

  auto* removed_tunnels = message.mutable_removed_tunnel_names();
  for (auto& disconn : queued_updates_.disconnections) {
    *removed_tunnels->Add() = disconn->name;
  }

  message.set_full_push(full_push);
  message.set_nonce(++nonce_current_);
  stats_.nonce_current_gauge_.set(nonce_current_);

  ENVOY_LOG(debug,
            "GrpcClient: built request: cluster={}, full_push={}, add={}, remove={}, nonce={}",
            config_.cluster, full_push, queued_updates_.connections.size(),
            queued_updates_.disconnections.size(), message.nonce());

  stats_.sent_accepted_cnt_counter_.add(queued_updates_.connections.size());
  stats_.sent_removed_cnt_counter_.add(queued_updates_.disconnections.size());
  queued_updates_.clear();

  return message;
}

void GrpcClient::setTimer(Event::TimerPtr& timer, const std::chrono::milliseconds& ms) {
  if (timer->enabled()) {
    timer->disableTimer();
  }

  timer->enableTimer(ms);
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
