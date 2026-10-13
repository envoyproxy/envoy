#pragma once

#include "envoy/http/header_map.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace InjectedCredentials {
namespace Common {

/**
 * Credential injector injects credential to HTTP headers.
 */
class CredentialInjector {
public:
  virtual ~CredentialInjector() = default;

  /**
   * Inject credential to HTTP headers.
   * @param headers supplies the reference to HTTP headers. The credential will be injected into the
   * headers.
   * @param overwrite whether to overwrite the existing credential in the headers.
   * @return Status whether the injection is successful.
   */
  virtual absl::Status inject(Envoy::Http::RequestHeaderMap& headers, bool overwrite) PURE;
};

using CredentialInjectorSharedPtr = std::shared_ptr<CredentialInjector>;

} // namespace Common
} // namespace InjectedCredentials
} // namespace Http
} // namespace Extensions
} // namespace Envoy
