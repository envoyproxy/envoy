#pragma once

#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/network/address.h"
#include "envoy/network/socket.h"
#include "envoy/network/udp_packet_writer_handler.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/extensions/access_loggers/syslog/sender.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Syslog {

class SyslogAccessLogStats;

void accountWriteResult(const Api::IoCallUint64Result& result, SyslogAccessLogStats& stats);

class UdpDatagramWriter {
public:
  UdpDatagramWriter(Event::Dispatcher& dispatcher, SyslogAccessLogStats& stats);
  UdpDatagramWriter(Event::Dispatcher& dispatcher, SyslogAccessLogStats& stats,
                    Network::Address::InstanceConstSharedPtr destination);
  ~UdpDatagramWriter();

  void write(absl::string_view record, Network::Address::InstanceConstSharedPtr destination);

private:
  struct SocketState {
    Network::SocketPtr socket_;
    Network::UdpPacketWriterPtr writer_;
  };

  SocketState& stateFor(const Network::Address::Instance& destination);
  void initialize(Network::Address::InstanceConstSharedPtr destination);
  void resetFileEvents(SocketState& state);

  Event::Dispatcher& dispatcher_;
  SyslogAccessLogStats& stats_;
  SocketState ipv4_;
  SocketState ipv6_;
  SocketState pipe_;
};

class StaticUdpSender : public Sender {
public:
  StaticUdpSender(Event::Dispatcher& dispatcher,
                  Network::Address::InstanceConstSharedPtr destination,
                  SyslogAccessLogStats& stats);

  void send(absl::string_view record) override;

private:
  const Network::Address::InstanceConstSharedPtr destination_;
  UdpDatagramWriter writer_;
};

class ClusterUdpSender : public Sender {
public:
  ClusterUdpSender(Event::Dispatcher& dispatcher, Upstream::ClusterManager& cluster_manager,
                   absl::string_view cluster_name, SyslogAccessLogStats& stats);

  void send(absl::string_view record) override;

private:
  Upstream::ClusterManager& cluster_manager_;
  const std::string cluster_name_;
  UdpDatagramWriter writer_;
};

} // namespace Syslog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
