#pragma once

#include "envoy/common/optional.h"
#include "envoy/common/pure.h"
#include "envoy/common/time.h"

namespace Upstream {

class Host;
typedef std::shared_ptr<Host> HostPtr;

class HostDescription;
typedef std::shared_ptr<const HostDescription> HostDescriptionPtr;

namespace Outlier {

/**
 * Sink for per host data. Proxy filters should send pertinent data when available.
 */
class DetectorHostSink {
public:
  virtual ~DetectorHostSink() {}

  /**
   * @return the number of times this host has been ejected.
   */
  virtual uint32_t numEjections() PURE;

  /**
   * Add an HTTP response code for a host.
   */
  virtual void putHttpResponseCode(uint64_t code) PURE;

  /**
   * Add a response time for a host (in this case response time is generic and might be used for
   * different operations including HTTP, Mongo, Redis, etc.).
   */
  virtual void putResponseTime(std::chrono::milliseconds time) PURE;

  /**
   * Get the time of last ejection.
   * @return the last time this host was ejected
   */
  virtual Optional<SystemTime> lastEjectionTime() PURE;

  /**
   * Get the time of last unejection.
   * @return the last time this host was unejected
   */
  virtual Optional<SystemTime> lastUnejectionTime() PURE;
};

typedef std::unique_ptr<DetectorHostSink> DetectorHostSinkPtr;

enum class EjectionType { Consecutive5xx };

/**
 * Sink for outlier detection event logs.
 */
class EventLogger {
public:
  virtual ~EventLogger() {}

  /**
   * Log an ejection event.
   * @param host supplies the host that generated the event.
   * @param type supplies the type of the event.
   */
  virtual void logEject(HostDescriptionPtr host, EjectionType type) PURE;

  /**
   * Log an unejection event.
   * @param host supplies the host that generated the event.
   */
  virtual void logUneject(HostDescriptionPtr host) PURE;
};

typedef std::shared_ptr<EventLogger> EventLoggerPtr;

/**
 * Interface for an outlier detection engine. Uses per host data to determine which hosts in a
 * cluster are outliers and should be ejected.
 */
class Detector {
public:
  virtual ~Detector() {}

  /**
   * Outlier detection change state callback.
   */
  typedef std::function<void(HostPtr host)> ChangeStateCb;

  /**
   * Add a changed state callback to the detector. The callback will be called whenever any host
   * changes state (either ejected or brought back in) due to outlier status.
   */
  virtual void addChangedStateCb(ChangeStateCb cb) PURE;
};

typedef std::shared_ptr<Detector> DetectorPtr;

} // Outlier
} // Upstream
