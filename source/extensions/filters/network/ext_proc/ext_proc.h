#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/filters/network/ext_proc/v3/ext_proc.pb.h"
#include "envoy/extensions/filters/network/ext_proc/v3/ext_proc.pb.validate.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/service/network_ext_proc/v3/network_external_processor.pb.h"

#include "source/extensions/filters/network/ext_proc/client_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtProc {

#define ALL_NETWORK_EXT_PROC_FILTER_STATS(COUNTER)                                                 \
  COUNTER(streams_started)                                                                         \
  COUNTER(stream_msgs_sent)                                                                        \
  COUNTER(stream_msgs_received)                                                                    \
  COUNTER(read_data_sent)                                                                          \
  COUNTER(write_data_sent)                                                                         \
  COUNTER(read_data_injected)                                                                      \
  COUNTER(write_data_injected)                                                                     \
  COUNTER(empty_response_received)                                                                 \
  COUNTER(spurious_msgs_received)                                                                  \
  COUNTER(streams_closed)                                                                          \
  COUNTER(streams_grpc_error)                                                                      \
  COUNTER(streams_grpc_close)                                                                      \
  COUNTER(connections_closed)                                                                      \
  COUNTER(failure_mode_allowed)                                                                    \
  COUNTER(stream_open_failures)

struct NetworkExtProcStats {
  ALL_NETWORK_EXT_PROC_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Global configuration for Network ExtProc filter.
 */
class Config {
public:
  Config(const envoy::extensions::filters::network::ext_proc::v3::NetworkExternalProcessor& config,
         Stats::Scope& scope)
      : failure_mode_allow_(config.failure_mode_allow()),
        processing_mode_(config.processing_mode()), grpc_service_(config.grpc_service()),
        untyped_forwarding_namespaces_(
            config.metadata_options().forwarding_namespaces().untyped().begin(),
            config.metadata_options().forwarding_namespaces().untyped().end()),
        typed_forwarding_namespaces_(
            config.metadata_options().forwarding_namespaces().typed().begin(),
            config.metadata_options().forwarding_namespaces().typed().end()),
        stats_(generateStats(config.stat_prefix(), scope)) {};

  bool failureModeAllow() const { return failure_mode_allow_; }

  const envoy::extensions::filters::network::ext_proc::v3::ProcessingMode& processingMode() const {
    return processing_mode_;
  }

  const envoy::config::core::v3::GrpcService& grpcService() const { return grpc_service_; }

  const std::vector<std::string>& untypedForwardingMetadataNamespaces() const {
    return untyped_forwarding_namespaces_;
  }

  const std::vector<std::string>& typedForwardingMetadataNamespaces() const {
    return typed_forwarding_namespaces_;
  }

  const NetworkExtProcStats& stats() const { return stats_; }

private:
  NetworkExtProcStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    const std::string final_prefix = absl::StrCat("network_ext_proc.", prefix);
    return {ALL_NETWORK_EXT_PROC_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
  }

  const bool failure_mode_allow_;
  const envoy::extensions::filters::network::ext_proc::v3::ProcessingMode processing_mode_;
  const envoy::config::core::v3::GrpcService grpc_service_;
  const std::vector<std::string> untyped_forwarding_namespaces_;
  const std::vector<std::string> typed_forwarding_namespaces_;
  NetworkExtProcStats stats_;
};

using ConfigConstSharedPtr = std::shared_ptr<const Config>;
using ProcessingRequest = envoy::service::network_ext_proc::v3::ProcessingRequest;
using ProcessingResponse = envoy::service::network_ext_proc::v3::ProcessingResponse;

class NetworkExtProcFilter : public Envoy::Network::Filter,
                             ExternalProcessorCallbacks,
                             Envoy::Logger::Loggable<Envoy::Logger::Id::ext_proc> {
public:
  // The result of an attempt to open the stream
  enum class StreamOpenState {
    // The stream was opened successfully
    Ok,
    // The stream was not opened successfully and an error was delivered
    // downstream -- processing should stop
    Error,
    // The stream was not opened successfully but processing should
    // continue as if the stream was already closed.
    IgnoreError,
  };

  NetworkExtProcFilter(ConfigConstSharedPtr config, ExternalProcessorClientPtr&& client);
  ~NetworkExtProcFilter() override;

  // Network::ReadFilter
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;
  Envoy::Network::FilterStatus onNewConnection() override;
  Envoy::Network::FilterStatus onData(Envoy::Buffer::Instance& data, bool end_stream) override;

  // Network::WriteFilter
  void initializeWriteFilterCallbacks(Envoy::Network::WriteFilterCallbacks& callbacks) override;
  Envoy::Network::FilterStatus onWrite(Envoy::Buffer::Instance& data, bool end_stream) override;

  void onDownstreamEvent(Envoy::Network::ConnectionEvent event);

  // Delay close management
  void updateCloseCallbackStatus(bool enable, bool is_read);

  // ExternalProcessorCallbacks
  void onReceiveMessage(std::unique_ptr<ProcessingResponse>&& res) override;
  void onGrpcClose() override;
  void onGrpcError(Grpc::Status::GrpcStatus error, const std::string& message) override;
  void logStreamInfo() override {};
  void onComplete(ProcessingResponse&) override {};
  void onError() override {};

private:
  struct DownstreamCallbacks : public Envoy::Network::ConnectionCallbacks {
    DownstreamCallbacks(NetworkExtProcFilter& parent) : parent_(parent) {}

    // Network::ConnectionCallbacks
    void onEvent(Envoy::Network::ConnectionEvent event) override {
      parent_.onDownstreamEvent(event);
    }
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    NetworkExtProcFilter& parent_;
  };

  // Stream management
  StreamOpenState openStream();
  void closeStream();

  void sendRequest(Envoy::Buffer::Instance& data, bool end_stream, bool is_read);
  void addDynamicMetadata(ProcessingRequest& req);

  Envoy::Network::FilterStatus handleStreamError();
  void closeConnection(const std::string& reason);

  Envoy::Network::ReadFilterCallbacks* read_callbacks_{nullptr};
  Envoy::Network::WriteFilterCallbacks* write_callbacks_{nullptr};

  const ConfigConstSharedPtr config_;
  const NetworkExtProcStats& stats_;
  ExternalProcessorClientPtr client_;
  ExternalProcessorStreamPtr stream_;
  const Envoy::Grpc::GrpcServiceConfigWithHashKey config_with_hash_key_;
  Http::StreamFilterSidestreamWatermarkCallbacks watermark_callbacks_{};
  DownstreamCallbacks downstream_callbacks_;

  bool processing_complete_{false};

  // Delay close counters
  uint32_t disable_count_write_{0};
  uint32_t disable_count_read_{0};
};

} // namespace ExtProc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
