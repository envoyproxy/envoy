#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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
        processing_mode_(config.processing_mode()) {};

  bool failureModeAllow() const { return failure_mode_allow_; }

  const envoy::extensions::filters::network::ext_proc::v3::ProcessingMode& processingMode() const {
    return processing_mode_;
  }

private:
  bool failure_mode_allow_;
  envoy::extensions::filters::network::ext_proc::v3::ProcessingMode processing_mode_;
};

using ConfigSharedPtr = std::shared_ptr<Config>;

class NetworkExtProcFilter : public Envoy::Network::Filter,
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

  NetworkExtProcFilter(ConfigSharedPtr config, ExternalProcessorClientPtr&& client) : 
    config_(config), client_(std::move(client)) {}
  ~NetworkExtProcFilter() override;

  // Network::ReadFilter
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

  Envoy::Network::FilterStatus onNewConnection() override;

  Envoy::Network::FilterStatus onData(Envoy::Buffer::Instance& data, bool end_stream) override;

  // Network::WriteFilter
  void initializeWriteFilterCallbacks(Envoy::Network::WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
  }
  Envoy::Network::FilterStatus onWrite(Envoy::Buffer::Instance& data, bool end_stream) override;

private:
  StreamOpenState openStream();
  void sendRequest(Envoy::Buffer::Instance& data, bool end_stream, bool is_read);
  void onReceiveMessage(std::unique_ptr<ProcessingResponse>&& response);
  void onGrpcClose();
  void closeStream();

  Envoy::Network::ReadFilterCallbacks* read_callbacks_{};
  Envoy::Network::WriteFilterCallbacks* write_callbacks_{};
  ConfigSharedPtr config_;
  ExternalProcessorClientPtr client_;
  ExternalProcessorStreamPtr stream_;
  ::envoy::config::core::v3::GrpcService grpc_service_;
  Envoy::Grpc::GrpcServiceConfigWithHashKey config_with_hash_key_;
  bool processing_complete_;
};

} // namespace ExtProc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
