#pragma once

#include "envoy/upstream/outlier_detection.h"
#include "envoy/upstream/upstream.h"

#include "common/json/json_loader.h"

namespace Upstream {

/**
 * Null host sink implementation.
 */
class OutlierDetectorHostSinkNullImpl : public OutlierDetectorHostSink {
public:
  // Upstream::OutlierDetectorHostSink
  void putHttpResponseCode(uint64_t) override {}
  void putResponseTime(std::chrono::milliseconds) override {}
};

/**
 * Factory for creating a detector from a JSON configuration.
 */
class OutlierDetectorImplFactory {
public:
  static OutlierDetectorPtr createForCluster(Cluster& cluster, const Json::Object& cluster_config,
                                             Event::Dispatcher& dispatcher);
};

/**
 * Implementation of OutlierDetectorHostSink for the generic detector.
 */
class OutlierDetectorHostSinkImpl : public OutlierDetectorHostSink {
public:
  // Upstream::OutlierDetectorHostSink
  void putHttpResponseCode(uint64_t) override {}
  void putResponseTime(std::chrono::milliseconds) override {}
};

/**
 * An implementation of an outlier detector. In the future we may support multiple outlier detection
 * implementations with different configuration. For now, as we iterate everything is contained
 * within this implementation.
 */
class OutlierDetectorImpl : public OutlierDetector {
public:
  OutlierDetectorImpl(Cluster& cluster, Event::Dispatcher& dispatcher);

  // Upstream::OutlierDetector
  void addChangedStateCb(ChangeStateCb cb) override { callbacks_.push_back(cb); }

private:
  void addHostSink(HostPtr host);

  std::list<ChangeStateCb> callbacks_;
  std::unordered_map<HostPtr, OutlierDetectorHostSinkImpl*> host_sinks_;
};

} // Upstream
