#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

#include "envoy/common/optional.h"
#include "envoy/common/pure.h"
#include "envoy/common/time.h"

namespace Envoy {
namespace Upstream {

class Host;
typedef std::shared_ptr<Host> HostSharedPtr;

class HostDescription;
typedef std::shared_ptr<const HostDescription> HostDescriptionConstSharedPtr;

namespace Outlier {

/**
 * Non-HTTP result of requests/operations.
 */
enum class Result {
  SUCCESS,        // Successfully established a connection or completed a request.
  TIMEOUT,        // Timed out while connecting or executing a request.
  CONNECT_FAILED, // Remote host rejected the connection.

  // The entries below only make sense when Envoy understands requests/responses for the
  // protocol being proxied. They do not make sense for TcpProxy, for example.

  REQUEST_FAILED, // Request was not completed successfully.
  SERVER_FAILURE, // The server indicated it cannot process a request.
};

/**
 * Monitor for per host data. Proxy filters should send pertinent data when available.
 */
class DetectorHostMonitor {
public:
  virtual ~DetectorHostMonitor() {}

  /**
   * @return the number of times this host has been ejected.
   */
  virtual uint32_t numEjections() PURE;

  /**
   * Add an HTTP response code for a host.
   */
  virtual void putHttpResponseCode(uint64_t code) PURE;

  /**
   * Add a non-HTTP result for a host.
   */
  virtual void putResult(Result result) PURE;

  /**
   * Add a response time for a host (in this case response time is generic and might be used for
   * different operations including HTTP, Mongo, Redis, etc.).
   */
  virtual void putResponseTime(std::chrono::milliseconds time) PURE;

  /**
   * Get the time of last ejection.
   * @return the last time this host was ejected, if the host has been ejected previously.
   */
  virtual const Optional<MonotonicTime>& lastEjectionTime() PURE;

  /**
   * Get the time of last unejection.
   * @return the last time this host was unejected, if the host has been unejected previously.
   */
  virtual const Optional<MonotonicTime>& lastUnejectionTime() PURE;

  /**
   * @return the success rate of the host in the last calculated interval, in the range 0-100.
   *         -1 means that the host did not have enough request volume to calculate success rate
   *         or the cluster did not have enough hosts to run through success rate outlier ejection.
   */
  virtual double successRate() const PURE;
};

typedef std::unique_ptr<DetectorHostMonitor> DetectorHostMonitorPtr;

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
  typedef std::function<void(HostSharedPtr host)> ChangeStateCb;

  /**
   * Add a changed state callback to the detector. The callback will be called whenever any host
   * changes state (either ejected or brought back in) due to outlier status.
   */
  virtual void addChangedStateCb(ChangeStateCb cb) PURE;

  /**
   * Returns the average success rate of the hosts in the Detector for the last aggregation
   * interval.
   * @return the average success rate, or -1 if there were not enough hosts with enough request
   *         volume to proceed with success rate based outlier ejection.
   */
  virtual double successRateAverage() const PURE;

  /**
   * Returns the success rate threshold used in the last interval. The threshold is used to eject
   * hosts based on their success rate.
   * @return the threshold, or -1 if there were not enough hosts with enough request volume to
   *         proceed with success rate based outlier ejection.
   */
  virtual double successRateEjectionThreshold() const PURE;
};

typedef std::shared_ptr<Detector> DetectorSharedPtr;

enum class EjectionType { Consecutive5xx, SuccessRate, ConsecutiveGatewayFailure };

/**
 * Sink for outlier detection event logs.
 */
class EventLogger {
public:
  virtual ~EventLogger() {}

  /**
   * Log an ejection event.
   * @param host supplies the host that generated the event.
   * @param detector supplies the detector that is doing the ejection.
   * @param type supplies the type of the event.
   * @param enforced is true if the ejection took place; false, if only logging took place.
   */
  virtual void logEject(HostDescriptionConstSharedPtr host, Detector& detector, EjectionType type,
                        bool enforced) PURE;

  /**
   * Log an unejection event.
   * @param host supplies the host that generated the event.
   */
  virtual void logUneject(HostDescriptionConstSharedPtr host) PURE;
};

typedef std::shared_ptr<EventLogger> EventLoggerSharedPtr;

} // namespace Outlier
} // namespace Upstream
} // namespace Envoy
