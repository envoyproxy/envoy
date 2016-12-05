#include "outlier_detection_impl.h"

#include "envoy/event/dispatcher.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/http/codes.h"

namespace Upstream {
namespace Outlier {

DetectorPtr DetectorImplFactory::createForCluster(Cluster& cluster,
                                                  const Json::Object& cluster_config,
                                                  Event::Dispatcher& dispatcher,
                                                  Runtime::Loader& runtime, Stats::Store& stats,
                                                  EventLoggerPtr event_logger) {
  // Right now we don't support any configuration but in order to make the config backwards
  // compatible we just look for an empty object.
  if (cluster_config.hasObject("outlier_detection")) {
    return DetectorPtr{new DetectorImpl(cluster, dispatcher, runtime, stats,
                                        ProdSystemTimeSource::instance_, event_logger)};
  } else {
    return nullptr;
  }
}

void DetectorHostSinkImpl::eject(SystemTime ejection_time) {
  ASSERT(!host_.lock()->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK));
  host_.lock()->healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  num_ejections_++;
  ejection_time_ = ejection_time;
}

void DetectorHostSinkImpl::putHttpResponseCode(uint64_t response_code) {
  if (Http::CodeUtility::is5xx(response_code)) {
    if (++consecutive_5xx_ ==
        detector_.runtime().snapshot().getInteger("outlier_detection.consecutive_5xx", 5)) {
      detector_.onConsecutive5xx(host_.lock());
    }
  } else {
    consecutive_5xx_ = 0;
  }
}

DetectorImpl::DetectorImpl(Cluster& cluster, Event::Dispatcher& dispatcher,
                           Runtime::Loader& runtime, Stats::Store& stats,
                           SystemTimeSource& time_source, EventLoggerPtr event_logger)
    : dispatcher_(dispatcher), runtime_(runtime), time_source_(time_source),
      stats_(generateStats(cluster.name(), stats)),
      interval_timer_(dispatcher.createTimer([this]() -> void { onIntervalTimer(); })),
      event_logger_(event_logger) {
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
      if (host->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK)) {
        ASSERT(stats_.ejections_active_.value() > 0);
        stats_.ejections_active_.dec();
      }

      host_sinks_.erase(host);
    }
  });

  armIntervalTimer();
}

void DetectorImpl::addHostSink(HostPtr host) {
  ASSERT(host_sinks_.count(host) == 0);
  DetectorHostSinkImpl* sink = new DetectorHostSinkImpl(*this, host);
  host_sinks_[host] = sink;
  host->setOutlierDetector(DetectorHostSinkPtr{sink});
}

void DetectorImpl::armIntervalTimer() {
  interval_timer_->enableTimer(std::chrono::milliseconds(
      runtime_.snapshot().getInteger("outlier_detection.interval_ms", 10000)));
}

void DetectorImpl::checkHostForUneject(HostPtr host, DetectorHostSinkImpl* sink, SystemTime now) {
  if (!host->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK)) {
    return;
  }

  std::chrono::milliseconds base_eject_time = std::chrono::milliseconds(
      runtime_.snapshot().getInteger("outlier_detection.base_ejection_time_ms", 30000));
  ASSERT(sink->numEjections() > 0)
  if ((base_eject_time * sink->numEjections()) <= (now - sink->ejectionTime())) {
    stats_.ejections_active_.dec();
    host->healthFlagClear(Host::HealthFlag::FAILED_OUTLIER_CHECK);
    runCallbacks(host);

    if (event_logger_) {
      event_logger_->logUneject(host);
    }
  }
}

void DetectorImpl::ejectHost(HostPtr host, EjectionType type) {
  uint64_t max_ejection_percent =
      std::min(100UL, runtime_.snapshot().getInteger("outlier_detection.max_ejection_percent", 10));
  if ((stats_.ejections_active_.value() / host_sinks_.size()) < max_ejection_percent) {
    stats_.ejections_total_.inc();
    if (runtime_.snapshot().featureEnabled("outlier_detection.enforcing", 100)) {
      stats_.ejections_active_.inc();
      host_sinks_[host]->eject(time_source_.currentSystemTime());
      runCallbacks(host);

      if (event_logger_) {
        event_logger_->logEject(host, type);
      }
    }
  } else {
    stats_.ejections_overflow_.inc();
  }
}

DetectionStats DetectorImpl::generateStats(const std::string& name, Stats::Store& store) {
  std::string prefix(fmt::format("cluster.{}.outlier_detection.", name));
  return {ALL_OUTLIER_DETECTION_STATS(POOL_COUNTER_PREFIX(store, prefix),
                                      POOL_GAUGE_PREFIX(store, prefix))};
}

void DetectorImpl::onConsecutive5xx(HostPtr host) {
  // This event will come from all threads, so we synchronize with a post to the main thread.
  dispatcher_.post([this, host]() -> void { onConsecutive5xxWorker(host); });
}

void DetectorImpl::onConsecutive5xxWorker(HostPtr host) {
  // This comes in cross thread. There is a chance that the host has already been removed from
  // the set. If so, just ignore it.
  if (host_sinks_.count(host) == 0) {
    return;
  }

  if (host->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK)) {
    return;
  }

  stats_.ejections_consecutive_5xx_.inc();
  ejectHost(host, EjectionType::Consecutive5xx);
}

void DetectorImpl::onIntervalTimer() {
  SystemTime now = time_source_.currentSystemTime();
  for (auto host : host_sinks_) {
    checkHostForUneject(host.first, host.second, now);
  }

  armIntervalTimer();
}

void DetectorImpl::runCallbacks(HostPtr host) {
  for (ChangeStateCb cb : callbacks_) {
    cb(host);
  }
}

void EventLoggerImpl::logEject(HostDescriptionPtr host, EjectionType type) {
  // TODO: Log friendly host name (e.g., instance ID or DNS name).
  // clang-format off
  static const std::string json =
    std::string("{{") +
    "\"time\": \"{}\", " +
    "\"cluster\": \"{}\", " +
    "\"upstream_ip\": \"{}\", " +
    "\"action\": \"eject\", " +
    "\"type\": \"{}\"" +
    "}}\n";
  // clang-format on

  file_->write(fmt::format(json,
                           AccessLogDateTimeFormatter::fromTime(time_source_.currentSystemTime()),
                           host->cluster().name(), host->url(), typeToString(type)));
}

void EventLoggerImpl::logUneject(HostDescriptionPtr host) {
  // TODO: Log friendly host name (e.g., instance ID or DNS name).
  // clang-format off
  static const std::string json =
    std::string("{{") +
    "\"time\": \"{}\", " +
    "\"cluster\": \"{}\", " +
    "\"upstream_ip\": \"{}\", " +
    "\"action\": \"uneject\""
    "}}\n";
  // clang-format on

  file_->write(fmt::format(json,
                           AccessLogDateTimeFormatter::fromTime(time_source_.currentSystemTime()),
                           host->cluster().name(), host->url()));
}

std::string EventLoggerImpl::typeToString(EjectionType type) {
  switch (type) {
  case EjectionType::Consecutive5xx:
    return "5xx";
  }

  NOT_IMPLEMENTED;
}

} // Outlier
} // Upstream
