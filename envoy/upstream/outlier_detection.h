#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/config/typed_config.h"
#include "envoy/data/cluster/v3/outlier_detection_event.pb.h"
#include "envoy/protobuf/message_validator.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Upstream {

class Host;
using HostSharedPtr = std::shared_ptr<Host>;

class HostDescription;
using HostDescriptionConstSharedPtr = std::shared_ptr<const HostDescription>;

namespace Outlier {

/**
 * Non-HTTP result of requests/operations.
 */
enum class Result {
  // Local origin errors detected by Envoy.
  LocalOriginTimeout,             // Timed out while connecting or executing a request.
  LocalOriginConnectFailed,       // Remote host rejected the connection.
  LocalOriginConnectSuccess,      // Successfully established a connection to upstream host.
                                  // Use this code when there is another protocol on top of
                                  // transport protocol. For example HTTP runs on top of tcp.
                                  // The same for redis. It first establishes TCP and then runs
                                  // a transaction.
  LocalOriginConnectSuccessFinal, // Successfully established a connection to upstream host
                                  // Use this code when there is no other protocol on top of the
                                  // protocol used by a filter. For example tcp_proxy filter
                                  // serves only tcp level. There is no other protocol on top of
                                  // tcp which the tcp_proxy filter is aware of.

  // The entries below only make sense when Envoy understands requests/responses for the
  // protocol being proxied. They do not make sense for TcpProxy, for example.
  // External origin errors.
  ExtOriginRequestFailed, // The server indicated it cannot process a request
  ExtOriginRequestSuccess // Request was completed successfully.
};

// Types of errors reported to outlier detectors.
// Each type may have a different syntax and content.
// TODO (cpakulski) - maybe use enums from protobufs.
enum class ExtResultType {
  HTTP_CODE,
  LOCAL_ORIGIN,
};

/*
 * Class carries result of a transaction with upstream entity
 * or generated internally by Envoy.
 * Different categories of results will be derived from that base class.
 * Those categories of results are fed only into Outlier Detection extensions.
 */
class ExtResult {
public:
  ExtResult() = delete;
  ExtResult(ExtResultType type) : type_(type) {}
  virtual ExtResultType type() const { return type_; };
  virtual ~ExtResult() = default;

private:
  const ExtResultType type_;
};

class HttpCode : public ExtResult {
public:
  HttpCode(uint32_t code) : ExtResult(ExtResultType::HTTP_CODE), code_(code) {}
  HttpCode() = delete;
  virtual ~HttpCode() {}
  uint32_t code() const { return code_; }

private:
  uint32_t code_;
};

// LocalOriginEvent is used to report errors like resets, timeouts but also
// successful connection attempts.
class LocalOriginEvent : public ExtResult {
public:
  LocalOriginEvent(Result result) : ExtResult(ExtResultType::LOCAL_ORIGIN), result_(result) {}
  LocalOriginEvent() = delete;
  Result result() const { return result_; }

private:
  Result result_;
};

// Base class for various types of monitors.
// Each monitor may implement different health detection algorithm.
class ExtMonitor {
public:
  ExtMonitor(const std::string& name, uint32_t enforce) : name_(name), enforce_(enforce) {}
  ExtMonitor() = delete;
  virtual ~ExtMonitor() {}
  virtual void reportResult(const ExtResult&) PURE;

  void
  setCallback(std::function<void(uint32_t, std::string, absl::optional<std::string>)> callback) {
    callback_ = callback;
  }

  void reset() { onReset(); }
  std::string name() const { return name_; }

protected:
  virtual bool onError() PURE;
  virtual void onSuccess() PURE;
  virtual void onReset() PURE;
  virtual std::string getFailedExtraInfo() { return ""; }

  std::string name_;
  uint32_t enforce_{100};
  std::function<void(uint32_t, std::string, absl::optional<std::string>)> callback_;
};

using ExtMonitorPtr = std::unique_ptr<ExtMonitor>;

class ExtMonitorsSet {
public:
  void addMonitor(ExtMonitorPtr&& monitor) { monitors_.push_back(std::move(monitor)); }
  template <class CALLABLE> void forEach(const CALLABLE& f) const {
    for (auto& monitor : monitors_) {
      f(monitor);
    }
  }
  bool empty() const { return monitors_.empty(); }

private:
  absl::InlinedVector<ExtMonitorPtr, 3> monitors_;
};
/**
 * Monitor for per host data. Proxy filters should send pertinent data when available.
 */
class DetectorHostMonitor {
public:
  // Types of Success Rate monitors.
  enum class SuccessRateMonitorType { ExternalOrigin, LocalOrigin };

  virtual ~DetectorHostMonitor() = default;

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
   * Some non-HTTP codes like TIMEOUT may require special mapping to HTTP code
   * and such code may be passed as optional parameter.
   */
  virtual void putResult(Result result, absl::optional<uint64_t> code) PURE;

  /**
   * Wrapper around putResult with 2 params when mapping to HTTP code is not
   * required.
   */
  void putResult(Result result) { putResult(result, absl::nullopt); }

  /**
   * Add a response time for a host (in this case response time is generic and might be used for
   * different operations including HTTP, Mongo, Redis, etc.).
   */
  virtual void putResponseTime(std::chrono::milliseconds time) PURE;

  /**
   * Get the time of last ejection.
   * @return the last time this host was ejected, if the host has been ejected previously.
   */
  virtual const absl::optional<MonotonicTime>& lastEjectionTime() PURE;

  /**
   * Get the time of last unejection.
   * @return the last time this host was unejected, if the host has been unejected previously.
   */
  virtual const absl::optional<MonotonicTime>& lastUnejectionTime() PURE;

  /**
   * @return the success rate of the host in the last calculated interval, in the range 0-100.
   *         -1 means that the host did not have enough request volume to calculate success rate
   *         or the cluster did not have enough hosts to run through success rate outlier ejection.
   * @param type specifies for which Success Rate Monitor the success rate value should be returned.
   *         If the outlier detector is configured not to split external and local origin errors,
   *         ExternalOrigin type returns success rate for all types of errors: external and local
   * origin and LocalOrigin type returns -1. If the outlier detector is configured to split external
   * and local origin errors, ExternalOrigin type returns success rate for external origin errors
   * and LocalOrigin type returns success rate for local origin errors.
   */
  virtual double successRate(SuccessRateMonitorType type) const PURE;

  /**
     @return name of failed extension monitor or empty string if failure was not in extensions.
  */
  virtual std::string getFailedExtensionMonitorName() const PURE;

  /**
     @return extra info which extension monitor wants to add to event logger
             or empty string if failure was not in extensions.
  */
  virtual std::string getFailedExtensionMonitorExtraInfo() const PURE;

  /**
     @return ejection enforcing parameter configured in extension
              monitor which failed.
  */
  virtual uint32_t getFailedExtensionMonitorEnforce() const PURE;
};

using DetectorHostMonitorPtr = std::unique_ptr<DetectorHostMonitor>;

/**
 * Interface for an outlier detection engine. Uses per host data to determine which hosts in a
 * cluster are outliers and should be ejected.
 */
class Detector {
public:
  virtual ~Detector() = default;

  /**
   * Outlier detection change state callback.
   */
  using ChangeStateCb = std::function<void(const HostSharedPtr& host)>;

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
   * @param type - see DetectorHostMonitor::successRate.
   */
  virtual double successRateAverage(DetectorHostMonitor::SuccessRateMonitorType) const PURE;

  /**
   * Returns the success rate threshold used in the last interval. The threshold is used to eject
   * hosts based on their success rate.
   * @return the threshold, or -1 if there were not enough hosts with enough request volume to
   *         proceed with success rate based outlier ejection.
   */
  virtual double
      successRateEjectionThreshold(DetectorHostMonitor::SuccessRateMonitorType) const PURE;
};

using DetectorSharedPtr = std::shared_ptr<Detector>;

/**
 * Sink for outlier detection event logs.
 */
class EventLogger {
public:
  virtual ~EventLogger() = default;

  /**
   * Log an ejection event.
   * @param host supplies the host that generated the event.
   * @param detector supplies the detector that is doing the ejection.
   * @param type supplies the type of the event.
   * @param enforced is true if the ejection took place; false, if only logging took place.
   */
  virtual void logEject(const HostDescriptionConstSharedPtr& host, Detector& detector,
                        envoy::data::cluster::v3::OutlierEjectionType type, bool enforced) PURE;

  /**
   * Log an unejection event.
   * @param host supplies the host that generated the event.
   */
  virtual void logUneject(const HostDescriptionConstSharedPtr& host) PURE;
};

using EventLoggerSharedPtr = std::shared_ptr<EventLogger>;

using ExtMonitorCreateFn = std::function<ExtMonitorPtr()>;

class ExtMonitorFactoryContext {
public:
  ExtMonitorFactoryContext(ProtobufMessage::ValidationVisitor& validation_visitor)
      : validation_visitor_(validation_visitor) {}
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() const {
    return validation_visitor_;
  }

private:
  ProtobufMessage::ValidationVisitor& validation_visitor_;
};

class ExtMonitorFactory : public Envoy::Config::TypedFactory {
public:
  ~ExtMonitorFactory() override = default;

  virtual ExtMonitorCreateFn createMonitor(const std::string& name,
                                           ProtobufTypes::MessagePtr&& config,
                                           const ExtMonitorFactoryContext& context) PURE;

  std::string category() const override { return "envoy.outlier_detection_monitors"; }
};

} // namespace Outlier
} // namespace Upstream
} // namespace Envoy
