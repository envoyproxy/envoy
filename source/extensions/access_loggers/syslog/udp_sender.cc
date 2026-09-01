#include "source/extensions/access_loggers/syslog/udp_sender.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/network/socket_impl.h"
#include "source/common/network/udp_packet_writer_handler_impl.h"
#include "source/extensions/access_loggers/syslog/stats.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Syslog {

UdpDatagramWriter::UdpDatagramWriter(Event::Dispatcher& dispatcher, SyslogAccessLogStats& stats)
    : dispatcher_(dispatcher), stats_(stats) {}

UdpDatagramWriter::UdpDatagramWriter(Event::Dispatcher& dispatcher, SyslogAccessLogStats& stats,
                                     Network::Address::InstanceConstSharedPtr destination)
    : UdpDatagramWriter(dispatcher, stats) {
  ensureInitialized(std::move(destination));
}

UdpDatagramWriter::~UdpDatagramWriter() {
  resetFileEvents(ipv4_);
  resetFileEvents(ipv6_);
  resetFileEvents(pipe_);
}

void UdpDatagramWriter::resetFileEvents(SocketState& state) {
  if (state.socket_ != nullptr) {
    state.socket_->ioHandle().resetFileEvents();
  }
}

UdpDatagramWriter::SocketState&
UdpDatagramWriter::stateFor(const Network::Address::Instance& destination) {
  if (destination.type() == Network::Address::Type::Pipe) {
    return pipe_;
  }
  return destination.ip()->version() == Network::Address::IpVersion::v4 ? ipv4_ : ipv6_;
}

void UdpDatagramWriter::ensureInitialized(
    Network::Address::InstanceConstSharedPtr destination) {
  RELEASE_ASSERT(destination != nullptr, "Syslog UDP destination must not be null");
  SocketState& state = stateFor(*destination);
  if (state.socket_ != nullptr) {
    return;
  }

  state.socket_ = std::make_unique<Network::SocketImpl>(
      Network::Socket::Type::Datagram, destination, destination, Network::SocketCreationOptions{});
  state.writer_ = std::make_unique<Network::UdpDefaultWriter>(state.socket_->ioHandle());
  state.socket_->ioHandle().initializeFileEvent(
      dispatcher_,
      [socket = state.socket_.get(), writer = state.writer_.get()](uint32_t events) {
        ASSERT(events & Event::FileReadyType::Write);
        writer->setWritable();
        socket->ioHandle().enableFileEvents(0);
        return absl::OkStatus();
      },
      Event::PlatformDefaultTriggerType, 0);
}

void UdpDatagramWriter::write(absl::string_view record,
                              Network::Address::InstanceConstSharedPtr destination) {
  ensureInitialized(destination);
  SocketState& state = stateFor(*destination);

  if (state.writer_->isWriteBlocked()) {
    ENVOY_LOG_PERIODIC_MISC(
        warn, std::chrono::seconds(10),
        "Syslog writer for destination '{}' is blocked; dropping messages",
        destination->asString());
    return;
  }

  // UdpDefaultWriter reads the buffer synchronously and does not retain it. Reference the record
  // directly to avoid copying the formatted message.
  Buffer::BufferFragmentImpl fragment(record.data(), record.size(), nullptr);
  Buffer::OwnedImpl buffer;
  buffer.addBufferFragment(fragment);
  const Api::IoCallUint64Result result = state.writer_->writePacket(buffer, nullptr, *destination);
  if (state.writer_->isWriteBlocked()) {
    state.socket_->ioHandle().enableFileEvents(Event::FileReadyType::Write);
  }
  if (result.ok()) {
    stats_.sent(result.return_value_);
  } else {
    ENVOY_LOG_PERIODIC_MISC(
        warn, std::chrono::seconds(10),
        "Syslog write to destination '{}' failed: {}; dropping messages", destination->asString(),
        result.err_->getErrorDetails());
  }
}

StaticUdpSender::StaticUdpSender(Event::Dispatcher& dispatcher,
                                 Network::Address::InstanceConstSharedPtr destination,
                                 SyslogAccessLogStats& stats)
    : destination_(std::move(destination)), writer_(dispatcher, stats, destination_) {}

void StaticUdpSender::send(absl::string_view record) { writer_.write(record, destination_); }

ClusterUdpSender::ClusterUdpSender(Event::Dispatcher& dispatcher,
                                   Upstream::ClusterManager& cluster_manager,
                                   absl::string_view cluster_name, SyslogAccessLogStats& stats)
    : cluster_manager_(cluster_manager), cluster_name_(cluster_name), writer_(dispatcher, stats) {}

void ClusterUdpSender::send(absl::string_view record) {
  Upstream::ThreadLocalCluster* cluster = cluster_manager_.getThreadLocalCluster(cluster_name_);
  if (cluster == nullptr) {
    ENVOY_LOG_PERIODIC_MISC(warn, std::chrono::seconds(10),
                            "Syslog cluster '{}' is unavailable; dropping messages",
                            cluster_name_);
    return;
  }
  Upstream::HostConstSharedPtr host = Upstream::LoadBalancer::onlyAllowSynchronousHostSelection(
      cluster->loadBalancer().chooseHost(nullptr));
  if (host == nullptr) {
    ENVOY_LOG_PERIODIC_MISC(warn, std::chrono::seconds(10),
                            "Syslog cluster '{}' has no available host; dropping messages",
                            cluster_name_);
    return;
  }
  if (host->address()->type() != Network::Address::Type::Ip) {
    ENVOY_LOG_PERIODIC_MISC(
        warn, std::chrono::seconds(10),
        "Syslog cluster '{}' selected a non-IP host address; dropping messages", cluster_name_);
    return;
  }
  writer_.write(record, host->address());
}

} // namespace Syslog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
