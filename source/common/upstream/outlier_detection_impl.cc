#include "outlier_detection_impl.h"

#include "common/common/assert.h"

namespace Upstream {

OutlierDetectorPtr OutlierDetectorImplFactory::createForCluster(Cluster& cluster,
                                                                const Json::Object& cluster_config,
                                                                Event::Dispatcher& dispatcher) {
  // Right now we don't support any configuration but in order to make the config backwards
  // compatible we just look for an empty object.
  if (cluster_config.hasObject("outlier_detection")) {
    return OutlierDetectorPtr{new OutlierDetectorImpl(cluster, dispatcher)};
  } else {
    return nullptr;
  }
}

OutlierDetectorImpl::OutlierDetectorImpl(Cluster& cluster, Event::Dispatcher&) {
  for (HostPtr host : cluster.hosts()) {
    addHostSink(host);
  }

  cluster.addMemberUpdateCb([this](const std::vector<HostPtr>& hosts_added,
                                   const std::vector<HostPtr>& hosts_removed) -> void {
    for (HostPtr host : hosts_added) {
      addHostSink(host);
    }

    for (HostPtr host : hosts_removed) {
      ASSERT(host_sinks_.count(host) == 1);
      host_sinks_.erase(host);
    }
  });
}

void OutlierDetectorImpl::addHostSink(HostPtr host) {
  ASSERT(host_sinks_.count(host) == 0);
  OutlierDetectorHostSinkImpl* sink = new OutlierDetectorHostSinkImpl();
  host_sinks_[host] = sink;
  host->setOutlierDetector(OutlierDetectorHostSinkPtr{sink});
}

} // Upstream
