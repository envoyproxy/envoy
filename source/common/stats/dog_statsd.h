#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/stats/stats.h"

#include "common/stats/statsd.h"

namespace Envoy {
namespace Stats {
namespace DogStatsd {

class Writer : public Statsd::Writer {
public:
  Writer(Network::Address::InstanceConstSharedPtr address);

  void writeCounter(const std::string& name, const std::string& tag_str, uint64_t increment);
  void writeGauge(const std::string& name, const std::string& tag_str, uint64_t value);
  void writeTimer(const std::string& name, const std::string& tag_str,
                  const std::chrono::milliseconds& ms);
};

class UdpStatsdSink : public Statsd::UdpStatsdSink {
public:
  UdpStatsdSink(ThreadLocal::SlotAllocator& tls, Network::Address::InstanceConstSharedPtr address);

  void flushCounter(const Counter& counter, uint64_t delta);
  void flushGauge(const Gauge& gauge, uint64_t value);
  void onHistogramComplete(const Histogram& histogram, uint64_t value);
};

const std::string buildTagStr(const std::vector<Tag>& tags);

} // namespace DogStatsd
} // namespace Stats
} // namespace Envoy
