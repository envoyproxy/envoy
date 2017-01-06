#pragma once

#include "envoy/stats/stats.h"

namespace Stats {

class ScopeImpl : public Scope {
public:
  ScopeImpl(Stats::Store& parent, const std::string& prefix) : parent_(parent), prefix_(prefix) {}

  // Stats::Scope
  void deliverHistogramToSinks(const std::string& name, uint64_t value) override {
    parent_.deliverHistogramToSinks(name, value);
  }
  void deliverTimingToSinks(const std::string& name, std::chrono::milliseconds ms) {
    parent_.deliverTimingToSinks(name, ms);
  }

  Counter& counter(const std::string& name) override;
  Gauge& gauge(const std::string& name) override;
  Timer& timer(const std::string& name) override;

private:
  Stats::Store& parent_;
  const std::string prefix_;
};

} // Stats
