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
enum ProcessingState {
  // Successfully got the resources or message.
  RECEIVED = 0,
  // Failed to apply the resources.
  FAILED = 1,
};

struct ProcessingDetails {
  ProcessingDetails(const ProcessingState state) : state_(state){};
  ProcessingDetails(const ProcessingState state, const ::google::rpc::Status error_detail)
      : state_(state), error_detail_(error_detail){};
  const ProcessingState state_;
  ::google::rpc::Status error_detail_;
};

/**
 * An interface for hooking into xDS update events to provide the ablility to use some external
 * processor in xDS update. This traker provides the process point when the discovery response
 * is received, when the resoures are successfully processed and applied, and when there is any
 * failure.
 *
 * Instance of this interface get invoked on the main Envoy thread. Thus, it is important
 * for implementations of this interface to not execute any blocking operations on the same
 * thread.
 */
class XdsConfigTracker {
public:
  virtual ~XdsConfigTracker() = default;

  /**
   * Invoked when SotW xDS configuration updates have been successfully parsed, applied on
   * the Envoy instance, and are about to be ACK'ed.
   *
   * @param type_url The type url of xDS message.
   * @param resources A list of decoded resources to add to the current state.
   */
  virtual void onConfigIngested(const absl::string_view type_url,
                                const std::vector<DecodedResourcePtr>& resources) PURE;

  /**
   * Invoked when Delta xDS configuration updates have been successfully parsed, applied on
   * the Envoy instance, and are about to be ACK'ed.
   *
   * @param type_url The type url of xDS message.
   * @param added_resources A list of decoded resources to add to the current state.
   * @param removed_resources A list of resources to remove from the current state.
   */
  virtual void
  onConfigIngested(const absl::string_view type_url,
                   const std::vector<DecodedResourcePtr>& added_resources,
                   const Protobuf::RepeatedPtrField<std::string>& removed_resources) PURE;

  /**
   * Invoked when received or failed to decode and apply the xds configs in the response.
   * The ProcessingState can be RECIEVED or FALIED.
   *
   * @param message The SotW discovery response message body.
   * @param details The process state and error details.
   */
  virtual void
  onConfigReceivedOrFailed(const envoy::service::discovery::v3::DiscoveryResponse& message,
                           const ProcessingDetails& details) PURE;

  /**
   * Invoked when received or failed to decode and apply the xds configs in the response.
   * The ProcessingState can be RECIEVED or FALIED.
   *
   * @param message The Delta discovery response message body.
   * @param details The process state and error details.
   */
  virtual void
  onConfigReceivedOrFailed(const envoy::service::discovery::v3::DeltaDiscoveryResponse& message,
                           const ProcessingDetails& details) PURE;
};

using XdsConfigTrackerPtr = std::unique_ptr<XdsConfigTracker>;
using XdsConfigTrackerOptRef = OptRef<XdsConfigTracker>;

/**
 * A factory abstract class for creating instances of XdsConfigTracker.
 */
class XdsConfigTrackerFactory : public Config::TypedFactory {
public:
  ~XdsConfigTrackerFactory() override = default;

  /**
   * Creates an XdsConfigTracker using the given config.
   */
  virtual XdsConfigTrackerPtr
  createXdsConfigTracker(const ProtobufWkt::Any& config,
                         ProtobufMessage::ValidationVisitor& validation_visitor) PURE;

  std::string category() const override { return "envoy.config.xds_trackers"; }
};

} // namespace Config
} // namespace Envoy
