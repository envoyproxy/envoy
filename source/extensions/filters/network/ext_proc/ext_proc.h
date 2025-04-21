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

/**
 * Global configuration for Network ExtProc filter.
 */
class Config {
public:
  Config(const envoy::extensions::filters::network::ext_proc::v3::NetworkExternalProcessor& config)
      : failure_mode_allow_(config.failure_mode_allow()),
        processing_mode_(config.processing_mode()), grpc_service_(config.grpc_service()) {};

  bool failureModeAllow() const { return failure_mode_allow_; }

  const envoy::extensions::filters::network::ext_proc::v3::ProcessingMode& processingMode() const {
    return processing_mode_;
  }

  const absl::optional<const envoy::config::core::v3::GrpcService> grpcService() const {
    return grpc_service_;
  }

private:
  bool failure_mode_allow_;
  envoy::extensions::filters::network::ext_proc::v3::ProcessingMode processing_mode_;
  envoy::config::core::v3::GrpcService grpc_service_;
};

using ConfigSharedPtr = std::shared_ptr<Config>;
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

  NetworkExtProcFilter(ConfigSharedPtr config, ExternalProcessorClientPtr&& client);
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

  Envoy::Network::FilterStatus handleStreamError();
  void closeConnection(const std::string& reason);

  Envoy::Network::ReadFilterCallbacks* read_callbacks_{nullptr};
  Envoy::Network::WriteFilterCallbacks* write_callbacks_{nullptr};

  ConfigSharedPtr config_;
  ExternalProcessorClientPtr client_;
  ExternalProcessorStreamPtr stream_;
  ::envoy::config::core::v3::GrpcService grpc_service_;
  Envoy::Grpc::GrpcServiceConfigWithHashKey config_with_hash_key_;
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
