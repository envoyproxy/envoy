#include "common/stats/statsd.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

namespace Stats {
namespace Statsd {

Writer::Writer(uint32_t port) {
  Network::Address::InstanceConstSharedPtr address(new Network::Address::Ipv4Instance(port));
  fd_ = address->socket(Network::Address::SocketType::Datagram);
  ASSERT(fd_ != -1);

  int rc = address->connect(fd_);
  ASSERT(rc != -1);
  UNREFERENCED_PARAMETER(rc);
}

Writer::~Writer() { close(fd_); }

void Writer::writeCounter(const std::string& name, uint64_t increment) {
  std::string message(fmt::format("envoy.{}:{}|c", name, increment));
  send(message);
}

void Writer::writeGauge(const std::string& name, uint64_t value) {
  std::string message(fmt::format("envoy.{}:{}|g", name, value));
  send(message);
}

void Writer::writeTimer(const std::string& name, const std::chrono::milliseconds& ms) {
  std::string message(fmt::format("envoy.{}:{}|ms", name, ms.count()));
  send(message);
}

void Writer::send(const std::string& message) {
  ::send(fd_, message.c_str(), message.size(), MSG_DONTWAIT);
}

void UdpStatsdSink::flushCounter(const std::string& name, uint64_t delta) {
  writer().writeCounter(name, delta);
}

void UdpStatsdSink::flushGauge(const std::string& name, uint64_t value) {
  writer().writeGauge(name, value);
}

void UdpStatsdSink::onTimespanComplete(const std::string& name, std::chrono::milliseconds ms) {
  writer().writeTimer(name, ms);
}

TcpStatsdSink::TcpStatsdSink(const LocalInfo::LocalInfo& local_info,
                             const std::string& cluster_name, ThreadLocal::Instance& tls,
                             Upstream::ClusterManager& cluster_manager)
    : local_info_(local_info), cluster_name_(cluster_name), tls_(tls),
      tls_slot_(tls.allocateSlot()), cluster_manager_(cluster_manager) {

  if (!cluster_manager.get(cluster_name)) {
    throw EnvoyException(fmt::format("unknown TCP statsd upstream cluster: {}", cluster_name));
  }

  if (local_info_.clusterName().empty() || local_info_.nodeName().empty()) {
    throw EnvoyException(
        fmt::format("TCP statsd requires setting --service-cluster and --service-node"));
  }

  tls.set(tls_slot_,
          [this](Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
            return std::make_shared<TlsSink>(*this, dispatcher);
          });
}

TcpStatsdSink::TlsSink::TlsSink(TcpStatsdSink& parent, Event::Dispatcher& dispatcher)
    : parent_(parent), dispatcher_(dispatcher) {}

TcpStatsdSink::TlsSink::~TlsSink() { ASSERT(!connection_); }

void TcpStatsdSink::TlsSink::flushCounter(const std::string& name, uint64_t delta) {
  write(fmt::format("envoy.{}:{}|c\n", name, delta));
}

void TcpStatsdSink::TlsSink::flushGauge(const std::string& name, uint64_t value) {
  write(fmt::format("envoy.{}:{}|g\n", name, value));
}

void TcpStatsdSink::TlsSink::onEvent(uint32_t events) {
  if (events & Network::ConnectionEvent::LocalClose ||
      events & Network::ConnectionEvent::RemoteClose) {
    dispatcher_.deferredDelete(std::move(connection_));
  }
}

void TcpStatsdSink::TlsSink::onTimespanComplete(const std::string& name,
                                                std::chrono::milliseconds ms) {
  write(fmt::format("envoy.{}:{}|ms\n", name, ms.count()));
}

void TcpStatsdSink::TlsSink::shutdown() {
  shutdown_ = true;
  if (connection_) {
    connection_->close(Network::ConnectionCloseType::NoFlush);
  }
}

void TcpStatsdSink::TlsSink::write(const std::string& stat) {
  if (shutdown_) {
    return;
  }

  if (!connection_) {
    Upstream::Host::CreateConnectionData info =
        parent_.cluster_manager_.tcpConnForCluster(parent_.cluster_name_);
    if (!info.connection_) {
      return;
    }

    connection_ = std::move(info.connection_);
    connection_->addConnectionCallbacks(*this);
    connection_->connect();
  }

  Buffer::OwnedImpl buffer(stat);
  connection_->write(buffer);
}

} // Statsd
} // Stats
