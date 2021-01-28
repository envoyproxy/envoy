#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/http/header_map.h"

namespace Envoy {
namespace Http {

/**
 * Interface class for IP detection extensions.
 */
class IPDetectionExtension {
public:
  virtual ~IPDetectionExtension() = default;

  /**
   * Detect the final remote address if any.
   *
   * @param request_headers supplies the incoming request headers.
   */
  virtual Network::Address::InstanceConstSharedPtr
  detect(Http::RequestHeaderMap& request_headers) PURE;
};

using IPDetectionExtensionSharedPtr = std::shared_ptr<IPDetectionExtension>;

/*
 * A factory for creating IP detection extensions.
 */
class IPDetectionExtensionFactory : public Envoy::Config::TypedFactory {
public:
  ~IPDetectionExtensionFactory() override = default;

  /**
   * Creates a particular Extension implementation.
   *
   * @param config supplies the configuration for the IP detection extension.
   * @return IPDetectionExtensionSharedPtr the extension instance.
   */
  virtual IPDetectionExtensionSharedPtr createExtension(const Protobuf::Message& config) const PURE;
};

using IPDetectionExtensionFactoryPtr = std::unique_ptr<IPDetectionExtensionFactory>;

} // namespace Http
} // namespace Envoy
