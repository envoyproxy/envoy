#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/network/address.h"

namespace Envoy {
namespace Http {

struct OriginalIPDetectionParams {
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
  std::string details;
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
   * Note that, when an extension is configured, this method will be called
   * ahead of the attempt to extract the downstream remote address from the
   * x-forwarded-for header. If the call fails to detect the original IP address,
   * the HCM will then fallback to the standard IP detection mechanisms.
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
   * Creates a particular extension implementation.
   *
   * @param config supplies the configuration for the original IP detection extension.
   * @return OriginalIPDetectionSharedPtr the extension instance.
   */
  virtual OriginalIPDetectionSharedPtr createExtension(const Protobuf::Message& config) const PURE;

  std::string category() const override { return "envoy.original_ip_detection"; }
};

using OriginalIPDetectionFactoryPtr = std::unique_ptr<OriginalIPDetectionFactory>;

} // namespace Http
} // namespace Envoy
