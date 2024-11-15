#include "source/common/upstream/health_checker_event_logger.h"

#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/network/utility.h"

namespace Envoy {
namespace Upstream {

void HealthCheckEventLoggerImpl::logEjectUnhealthy(
    envoy::data::core::v3::HealthCheckerType health_checker_type,
    const HostDescriptionConstSharedPtr& host,
    envoy::data::core::v3::HealthCheckFailureType failure_type) {
  createHealthCheckEvent(health_checker_type, *host, [&failure_type](auto& event) {
    event.mutable_eject_unhealthy_event()->set_failure_type(failure_type);
  });
}

void HealthCheckEventLoggerImpl::logUnhealthy(
    envoy::data::core::v3::HealthCheckerType health_checker_type,
    const HostDescriptionConstSharedPtr& host,
    envoy::data::core::v3::HealthCheckFailureType failure_type, bool first_check) {
  createHealthCheckEvent(health_checker_type, *host, [&first_check, &failure_type](auto& event) {
    event.mutable_health_check_failure_event()->set_failure_type(failure_type);
    event.mutable_health_check_failure_event()->set_first_check(first_check);
  });
}

void HealthCheckEventLoggerImpl::logAddHealthy(
    envoy::data::core::v3::HealthCheckerType health_checker_type,
    const HostDescriptionConstSharedPtr& host, bool first_check) {
  createHealthCheckEvent(health_checker_type, *host, [&first_check](auto& event) {
    event.mutable_add_healthy_event()->set_first_check(first_check);
  });
}

void HealthCheckEventLoggerImpl::logSuccessfulHealthCheck(
    envoy::data::core::v3::HealthCheckerType health_checker_type,
    const HostDescriptionConstSharedPtr& host) {
  createHealthCheckEvent(health_checker_type, *host,
                         [](auto& event) { event.mutable_successful_health_check_event(); });
}

void HealthCheckEventLoggerImpl::logDegraded(
    envoy::data::core::v3::HealthCheckerType health_checker_type,
    const HostDescriptionConstSharedPtr& host) {
  createHealthCheckEvent(health_checker_type, *host,
                         [](auto& event) { event.mutable_degraded_healthy_host(); });
}

void HealthCheckEventLoggerImpl::logNoLongerDegraded(
    envoy::data::core::v3::HealthCheckerType health_checker_type,
    const HostDescriptionConstSharedPtr& host) {
  createHealthCheckEvent(health_checker_type, *host,
                         [](auto& event) { event.mutable_no_longer_degraded_host(); });
}

void HealthCheckEventLoggerImpl::createHealthCheckEvent(
    envoy::data::core::v3::HealthCheckerType health_checker_type, const HostDescription& host,
    std::function<void(envoy::data::core::v3::HealthCheckEvent&)> callback) const {
  envoy::data::core::v3::HealthCheckEvent event;
  event.set_cluster_name(host.cluster().name());
  event.set_health_checker_type(health_checker_type);

  envoy::config::core::v3::Address address;
  Network::Utility::addressToProtobufAddress(*host.address(), address);
  *event.mutable_host() = std::move(address);
  if (host.metadata() != nullptr) {
    *event.mutable_metadata() = *host.metadata();
  }
  *event.mutable_locality() = host.locality();

  TimestampUtil::systemClockToTimestamp(time_source_.systemTime(), *event.mutable_timestamp());

  callback(event);
  for (const auto& event_sink : event_sinks_) {
    event_sink->log(event);
  }

#ifdef ENVOY_ENABLE_YAML
  if (file_ == nullptr) {
    return;
  }

  // Make sure the type enums make it into the JSON
  const auto json =
      MessageUtil::getJsonStringFromMessageOrError(event, /* pretty_print */ false,
                                                   /* always_print_primitive_fields */ true);
  file_->write(fmt::format("{}\n", json));

#endif
}
} // namespace Upstream
} // namespace Envoy
