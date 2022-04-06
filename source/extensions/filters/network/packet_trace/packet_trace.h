#pragma once
#include "envoy/extensions/filters/network/packet_trace/v3/packet_trace.pb.h"
#include "envoy/network/filter.h"

#include "source/common/common/logger.h"
#include "source/common/network/cidr_range.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PacketTrace {
/**
 * Confiugration for the PacketTrace filter.
 */
class PacketTraceConfig {
public:
  PacketTraceConfig(
      const envoy::extensions::filters::network::packet_trace::v3::PacketTrace& config)
      : local_address_range_(config.local_address_range()),
        remote_address_range_(config.remote_address_range()){};

  const Network::Address::IpList& packetTraceLocalAddress() { return local_address_range_; };
  const Network::Address::IpList& packetTraceRemoteAddress() { return remote_address_range_; };

private:
  Network::Address::IpList local_address_range_;
  Network::Address::IpList remote_address_range_;
};

using PacketTraceConfigSharedPtr = std::shared_ptr<PacketTraceConfig>;

/**
 * Implementation of a  packet  trace filter.
 */
class PacketTraceFilter : public Network::ReadFilter, Logger::Loggable<Logger::Id::filter> {
public:
  PacketTraceFilter(PacketTraceConfigSharedPtr config);

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

private:
  Network::ReadFilterCallbacks* read_callbacks_{};
  PacketTraceConfigSharedPtr config_;
};

} // namespace PacketTrace
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
