#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/network/address.h"
#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Http {

struct OriginalIPDetectionParams {
  // The request headers from downstream.
  //
  // Note that while extensions can modify the headers, they will undergo standard Envoy
  // sanitation after the detect() call so additions made here may be removed before
  // filters have access to headers.
  Http::RequestHeaderMap& request_headers;
  // The downstream directly connected address.
  const Network::Address::InstanceConstSharedPtr& downstream_remote_address;
};

// Parameters to be used for sending a local reply when detection fails.
struct OriginalIPRejectRequestOptions {
  Code response_code;
  std::string body;
};

struct OriginalIPDetectionResult {
  // An address that represents the detected address or nullptr if detection failed.
  Network::Address::InstanceConstSharedPtr detected_remote_address;
  // Is the detected address trusted (e.g.: can it be used to determine if this is an internal
  // request).
  bool allow_trusted_address_checks;
  // If set, these parameters will be used to signal that detection failed and the request should
  // be rejected.
  absl::optional<OriginalIPRejectRequestOptions> reject_options;
  // Whether to skip appending the detected remote address to ``x-forwarded-for``.
  bool skip_xff_append;
};

/**
 * Interface class for original IP detection extensions.
 */
class OriginalIPDetection {
public:
  virtual ~OriginalIPDetection() = default;

  /**
   * Detect the final remote address.
   *
   * If the call to this method succeeds in detecting the remote IP address or
   * fails and is configured to reject the request in that case, no other
   * configured extensions will be called (if any).
   *
   * @param param supplies the OriginalIPDetectionParams params for detection.
   * @return OriginalIPDetectionResult the result of the extension's attempt to detect
   * the final remote address.
   */
  virtual OriginalIPDetectionResult detect(OriginalIPDetectionParams& params) PURE;
};

using OriginalIPDetectionSharedPtr = std::shared_ptr<OriginalIPDetection>;

/*
 * A factory for creating original IP detection extensions.
 */
class OriginalIPDetectionFactory : public Envoy::Config::TypedFactory {
public:
  ~OriginalIPDetectionFactory() override = default;

  /**
   * Creates a particular extension implementation or return an error status.
   *
   * @param config supplies the configuration for the original IP detection extension.
   * @return OriginalIPDetectionSharedPtr the extension instance.
   */
  virtual absl::StatusOr<OriginalIPDetectionSharedPtr>
  createExtension(const Protobuf::Message& config,
                  Server::Configuration::FactoryContext& context) PURE;

  std::string category() const override { return "envoy.http.original_ip_detection"; }
};

using OriginalIPDetectionFactoryPtr = std::unique_ptr<OriginalIPDetectionFactory>;

} // namespace Http
} // namespace Envoy
