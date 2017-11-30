#include "common/stats/dog_statsd.h"

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/stats/stats.h"

#include "fmt/format.h"

namespace Envoy {
namespace Stats {
namespace DogStatsd {

Writer::Writer(Network::Address::InstanceConstSharedPtr address) : Statsd::Writer(address) {}

void Writer::writeCounter(const std::string& name, const std::string& tag_str, uint64_t increment) {
  std::string message(fmt::format("envoy.{}:{}|c{}\n", name, increment, tag_str));
  send(message);
}

void Writer::writeGauge(const std::string& name, const std::string& tag_str, uint64_t value) {
  std::string message(fmt::format("envoy.{}:{}|g{}\n", name, value, tag_str));
  send(message);
}

void Writer::writeTimer(const std::string& name, const std::string& tag_str,
                        const std::chrono::milliseconds& ms) {
  std::string message(fmt::format("envoy.{}:{}|ms{}\n", name, ms.count(), tag_str));
  send(message);
}

UdpStatsdSink::UdpStatsdSink(ThreadLocal::SlotAllocator& tls,
                             Network::Address::InstanceConstSharedPtr address)
    : Statsd::UdpStatsdSink(tls, address) {
  // Setting the DogStatsd::Writer explicitly.
  setWriter<Writer>();
}

void UdpStatsdSink::flushCounter(const Counter& counter, uint64_t delta) {
  tls_->getTyped<Writer>().writeCounter(counter.tagExtractedName(), buildTagStr(counter.tags()),
                                        delta);
}

void UdpStatsdSink::flushGauge(const Gauge& gauge, uint64_t value) {
  tls_->getTyped<Writer>().writeGauge(gauge.tagExtractedName(), buildTagStr(gauge.tags()), value);
}

void UdpStatsdSink::onHistogramComplete(const Histogram& histogram, uint64_t value) {
  // For statsd histograms are all timers.
  tls_->getTyped<Writer>().writeTimer(histogram.tagExtractedName(), buildTagStr(histogram.tags()),
                                      std::chrono::milliseconds(value));
}

const std::string buildTagStr(const std::vector<Tag>& tags) {
  if (tags.empty()) {
    return {};
  }

  std::string tagStr = "|#";
  int i = 0;
  for (const Tag& tag : tags) {
    if (i == 0) {
      tagStr.append(fmt::format("{}:{}", tag.name_, tag.value_));
    } else {
      tagStr.append(fmt::format(",{}:{}", tag.name_, tag.value_));
    }
    i++;
  }
  return tagStr;
}

} // namespace DogStatsd
} // namespace Stats
} // namespace Envoy
