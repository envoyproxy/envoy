#pragma once

#include <string>
#include <vector>

#include "envoy/common/optref.h"
#include "envoy/config/subscription.h"
#include "envoy/config/typed_config.h"
#include "envoy/protobuf/message_validator.h"

#include "source/common/protobuf/protobuf.h"

#include "google/rpc/status.pb.h"

namespace Envoy {
namespace Config {

// The status of processing Delta/DiscoveryResponse.
enum TraceState {
  // Successfully got the resources or message.
  RECEIVE = 0,
  // Successfully ingest the resouces.
  INGESTED = 1,
  // Fail to apply the resources.
  FAILED = 2,
};

struct TraceDetails {
  TraceDetails(const TraceState state) : state_(state){};
  TraceDetails(const TraceState state, const ::google::rpc::Status error_detail)
      : state_(state), error_detail_(error_detail){};
  const TraceState state_;
  ::google::rpc::Status error_detail_;
};

/**
 * An interface for hooking into xDS update events to provide the ablility to use some
 * external logger or processor in xDS update.
 *
 * Instance of this interface get invoked on the main Envoy thread. Thus, it is important
 * for implementations of this interface to not execute any blocking operations on the same
 * thread.
 */
class XdsConfigTracer {
public:
  virtual ~XdsConfigTracer() = default;

  /**
   * Log the decoded SotW xDS resources that are about to ingested.
   * @param type_url The type url of xDS message.
   * @param resources List of decoded resources that reflect the latest state.
   * @param details The log point state and details.
   */
  virtual void log(const absl::string_view type_url,
                   const std::vector<DecodedResourcePtr>& resources,
                   const TraceDetails& details) PURE;

  /**
   * Log point for SotW xDS discovery response.
   * applied on the Envoy instance, and are about to be ACK'ed.
   * @param message The SotW discovery response message body.
   * @param details The log point state and details.
   */
  virtual void log(const envoy::service::discovery::v3::DiscoveryResponse& message,
                   const TraceDetails& details) PURE;

  /**
   * Log point for Delta xDS discovery response message.
   * @param message The Delta discovery response message body.
   * @param details The log point state and details.
   */
  virtual void log(const envoy::service::discovery::v3::DeltaDiscoveryResponse& message,
                   const TraceDetails& details) PURE;
};

using XdsConfigTracerPtr = std::unique_ptr<XdsConfigTracer>;
using XdsConfigTracerOptRef = OptRef<XdsConfigTracer>;

/**
 * A factory abstract class for creating instances of XdsConfigTracer.
 */
class XdsConfigTracerFactory : public Config::TypedFactory {
public:
  ~XdsConfigTracerFactory() override = default;

  /**
   * Creates an XdsConfigTracer using the given config.
   */
  virtual XdsConfigTracerPtr
  createXdsConfigTracer(const ProtobufWkt::Any& config,
                        ProtobufMessage::ValidationVisitor& validation_visitor) PURE;

  std::string category() const override { return "envoy.config.xds_tracers"; }
};

} // namespace Config
} // namespace Envoy
