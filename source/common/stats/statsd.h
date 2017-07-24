#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/local_info/local_info.h"
#include "envoy/network/connection.h"
#include "envoy/stats/stats.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

namespace Envoy {
namespace Stats {
namespace Statsd {

/**
 * This is a simple UDP localhost writer for statsd messages.
 */
class Writer : public ThreadLocal::ThreadLocalObject {
public:
  Writer(Network::Address::InstanceConstSharedPtr address);
  ~Writer();

  void writeCounter(const std::string& name, uint64_t increment);
  void writeGauge(const std::string& name, uint64_t value);
  void writeTimer(const std::string& name, const std::chrono::milliseconds& ms);

  // Called in unit test to validate address.
  int getFdForTests() const { return fd_; };

private:
  void send(const std::string& message);

  int fd_;
};

/**
 * Implementation of Sink that writes to a UDP statsd address.
 */
class UdpStatsdSink : public Sink {
public:
  UdpStatsdSink(ThreadLocal::SlotAllocator& tls, Network::Address::InstanceConstSharedPtr address);

  // Stats::Sink
  void flushCounter(const std::string& name, uint64_t delta) override;
  void flushGauge(const std::string& name, uint64_t value) override;
  void onHistogramComplete(const std::string& name, uint64_t value) override {
    // For statsd histograms are just timers.
    onTimespanComplete(name, std::chrono::milliseconds(value));
  }
  void onTimespanComplete(const std::string& name, std::chrono::milliseconds ms) override;
  // Called in unit test to validate writer construction and address.
  int getFdForTests() { return tls_->getTyped<Writer>().getFdForTests(); }

private:
  ThreadLocal::SlotPtr tls_;
  Network::Address::InstanceConstSharedPtr server_address_;
};

/**
 * Per thread implementation of a TCP stats flusher for statsd.
 */
class TcpStatsdSink : public Sink {
public:
  TcpStatsdSink(const LocalInfo::LocalInfo& local_info, const std::string& cluster_name,
                ThreadLocal::SlotAllocator& tls, Upstream::ClusterManager& cluster_manager,
                Stats::Scope& scope);

  // Stats::Sink
  void flushCounter(const std::string& name, uint64_t delta) override {
    tls_->getTyped<TlsSink>().flushCounter(name, delta);
  }

  void flushGauge(const std::string& name, uint64_t value) override {
    tls_->getTyped<TlsSink>().flushGauge(name, value);
  }

  void onHistogramComplete(const std::string& name, uint64_t value) override {
    // For statsd histograms are just timers.
    onTimespanComplete(name, std::chrono::milliseconds(value));
  }

  void onTimespanComplete(const std::string& name, std::chrono::milliseconds ms) override {
    tls_->getTyped<TlsSink>().onTimespanComplete(name, ms);
  }

private:
  struct TlsSink : public ThreadLocal::ThreadLocalObject, public Network::ConnectionCallbacks {
    TlsSink(TcpStatsdSink& parent, Event::Dispatcher& dispatcher);
    ~TlsSink();

    void flushCounter(const std::string& name, uint64_t delta);
    void flushGauge(const std::string& name, uint64_t value);
    void onTimespanComplete(const std::string& name, std::chrono::milliseconds ms);
    void write(const std::string& stat);

    // Network::ConnectionCallbacks
    void onEvent(uint32_t events) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    TcpStatsdSink& parent_;
    Event::Dispatcher& dispatcher_;
    Network::ClientConnectionPtr connection_;
  };

  // Somewhat arbitrary 16MiB limit for buffered stats.
  static constexpr uint32_t MaxBufferedStatsBytes = (1024 * 1024 * 16);

  Upstream::ClusterInfoConstSharedPtr cluster_info_;
  ThreadLocal::SlotPtr tls_;
  Upstream::ClusterManager& cluster_manager_;
  Stats::Counter& cx_overflow_stat_;
};

} // namespace Statsd
} // namespace Stats
} // namespace Envoy
