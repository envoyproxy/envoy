#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/http/header_map.h"
#include "envoy/network/address.h"

namespace Envoy {
namespace Http {

struct OriginalIPDetectionParams {
  // Note: headers will be sanitized after the OriginalIPDetection::detect() call.
  Http::RequestHeaderMap& request_headers;
  // The downstream directly connected address.
  Network::Address::InstanceConstSharedPtr downstream_remote_address;
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
   * @return Address::InstanceConstSharedPtr an address that represents the detected address or
   * nullptr.
   */
  virtual Network::Address::InstanceConstSharedPtr
  detect(struct OriginalIPDetectionParams& params) PURE;
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
};

using OriginalIPDetectionFactoryPtr = std::unique_ptr<OriginalIPDetectionFactory>;

} // namespace Http
} // namespace Envoy
