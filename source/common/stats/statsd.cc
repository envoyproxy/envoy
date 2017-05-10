#include "common/stats/statsd.h"

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

#include "spdlog/spdlog.h"

namespace Envoy {
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
                             Upstream::ClusterManager& cluster_manager, Stats::Scope& scope)
    : local_info_(local_info), tls_(tls), tls_slot_(tls.allocateSlot()),
      cluster_manager_(cluster_manager), cx_overflow_stat_(scope.counter("statsd.cx_overflow")) {

  Upstream::ThreadLocalCluster* cluster = cluster_manager.get(cluster_name);
  if (!cluster) {
    throw EnvoyException(fmt::format("unknown TCP statsd upstream cluster: {}", cluster_name));
  }

  cluster_info_ = cluster->info();
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

  // Guard against the stats connection backing up. In this case we probably have no visibility
  // into what is going on externally, but we also increment a stat that should be viewable
  // locally.
  // NOTE: In the current implementation, we write most stats on the main thread, but timers
  //       get emitted on the worker threads. Since this is using global buffered data, it's
  //       possible that we are about to kill the connection that is not actually backed up.
  //       This is essentially a panic state, so it's not worth keeping per thread buffer stats,
  //       since if we stay over, the other threads will eventually kill their connections too.
  // TODO(mattklein123): The use of the stat is somewhat of a hack, and should be replaced with
  // real flow control callbacks once they are available.
  if (parent_.cluster_info_->stats().upstream_cx_tx_bytes_buffered_.value() >
      MaxBufferedStatsBytes) {
    if (connection_) {
      connection_->close(Network::ConnectionCloseType::NoFlush);
    }
    parent_.cx_overflow_stat_.inc();
    return;
  }

  if (!connection_) {
    Upstream::Host::CreateConnectionData info =
        parent_.cluster_manager_.tcpConnForCluster(parent_.cluster_info_->name());
    if (!info.connection_) {
      return;
    }

    connection_ = std::move(info.connection_);
    connection_->addConnectionCallbacks(*this);
    connection_->setBufferStats({parent_.cluster_info_->stats().upstream_cx_rx_bytes_total_,
                                 parent_.cluster_info_->stats().upstream_cx_rx_bytes_buffered_,
                                 parent_.cluster_info_->stats().upstream_cx_tx_bytes_total_,
                                 parent_.cluster_info_->stats().upstream_cx_tx_bytes_buffered_});
    connection_->connect();
  }

  Buffer::OwnedImpl buffer(stat);
  connection_->write(buffer);
}

} // Statsd
} // Stats
} // Envoy
