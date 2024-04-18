#pragma once

namespace Envoy {
namespace Tracing {

/**
 * The reasons why trace sampling may or may not be performed.
 */
enum class Reason {
  // Not sampled based on supplied request id or other reason.
  NotTraceable,
  // Not sampled due to being a health check.
  HealthCheck,
  // Sampling enabled.
  Sampling,
  // Sampling forced by the service.
  ServiceForced,
  // Sampling forced by the client.
  ClientForced,
};

/**
 * The decision regarding whether traces should be sampled, and the reason for it.
 */
struct Decision {
  Reason reason;
  bool traced;
};

} // namespace Tracing
} // namespace Envoy
