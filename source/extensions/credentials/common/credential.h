#pragma once

#include "envoy/http/header_map.h"

namespace Envoy {
namespace Extensions {
namespace Credentials {
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
   * @param overrite whether to overwrite the existing credential in the headers.
   *
   * @return true if the credential is injected successfully.
   */
  virtual bool inject(Http::RequestHeaderMap& headers, bool overrite) PURE;
};

using CredentialInjectorSharedPtr = std::shared_ptr<CredentialInjector>;

} // namespace Common
} // namespace Credentials
} // namespace Extensions
} // namespace Envoy
