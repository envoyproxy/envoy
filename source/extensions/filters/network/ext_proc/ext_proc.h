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
  NetworkExtProcFilter(ConfigSharedPtr config) : config_(config) {}
  ~NetworkExtProcFilter() = default;

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
  Envoy::Network::ReadFilterCallbacks* read_callbacks_{};
  Envoy::Network::WriteFilterCallbacks* write_callbacks_{};
  ConfigSharedPtr config_;
};

} // namespace ExtProc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
