#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/local_info/local_info.h"
#include "envoy/network/connection.h"
#include "envoy/stats/stats.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

namespace Stats {
namespace Statsd {

/**
 * This is a simple UDP localhost writer for statsd messages.
 */
class Writer {
public:
  Writer(uint32_t port);
  ~Writer();

  void writeCounter(const std::string& name, uint64_t increment);
  void writeGauge(const std::string& name, uint64_t value);
  void writeTimer(const std::string& name, const std::chrono::milliseconds& ms);

private:
  void send(const std::string& message);

  int fd_;
};

/**
 * Implementation of Sink that writes to a local UDP statsd port.
 */
class UdpStatsdSink : public Sink {
public:
  UdpStatsdSink(uint32_t port) : port_(port) {}

  // Stats::Sink
  void flushCounter(const std::string& name, uint64_t delta) override;
  void flushGauge(const std::string& name, uint64_t value) override;
  void onHistogramComplete(const std::string& name, uint64_t value) override {
    // For statsd histograms are just timers.
    onTimespanComplete(name, std::chrono::milliseconds(value));
  }
  void onTimespanComplete(const std::string& name, std::chrono::milliseconds ms) override;

private:
  Writer& writer() {
    static thread_local Statsd::Writer writer_(port_);
    return writer_;
  }

  uint32_t port_;
};

/**
 * Per thread implementation of a TCP stats flusher for statsd.
 */
class TcpStatsdSink : public Sink {
public:
  TcpStatsdSink(const LocalInfo::LocalInfo& local_info, const std::string& cluster_name,
                ThreadLocal::Instance& tls, Upstream::ClusterManager& cluster_manager);

  // Stats::Sink
  void flushCounter(const std::string& name, uint64_t delta) override {
    tls_.getTyped<TlsSink>(tls_slot_).flushCounter(name, delta);
  }

  void flushGauge(const std::string& name, uint64_t value) override {
    tls_.getTyped<TlsSink>(tls_slot_).flushGauge(name, value);
  }

  void onHistogramComplete(const std::string& name, uint64_t value) override {
    // For statsd histograms are just timers.
    onTimespanComplete(name, std::chrono::milliseconds(value));
  }

  void onTimespanComplete(const std::string& name, std::chrono::milliseconds ms) override {
    tls_.getTyped<TlsSink>(tls_slot_).onTimespanComplete(name, ms);
  }

private:
  struct TlsSink : public ThreadLocal::ThreadLocalObject, public Network::ConnectionCallbacks {
    TlsSink(TcpStatsdSink& parent, Event::Dispatcher& dispatcher);
    ~TlsSink();

    void flushCounter(const std::string& name, uint64_t delta);
    void flushGauge(const std::string& name, uint64_t value);
    void onTimespanComplete(const std::string& name, std::chrono::milliseconds ms);
    void write(const std::string& stat);

    // ThreadLocal::ThreadLocalObject
    void shutdown() override;

    // Network::ConnectionCallbacks
    void onEvent(uint32_t events) override;

    TcpStatsdSink& parent_;
    Event::Dispatcher& dispatcher_;
    Network::ClientConnectionPtr connection_;
    bool shutdown_{};
  };

  const LocalInfo::LocalInfo& local_info_;
  std::string cluster_name_;
  ThreadLocal::Instance& tls_;
  uint32_t tls_slot_;
  Upstream::ClusterManager& cluster_manager_;
};

} // Statsd
} // Stats
