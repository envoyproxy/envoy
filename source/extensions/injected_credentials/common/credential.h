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

  /*
   * Request for a credential from a given source.
   */
  class Request {
  public:
    virtual ~Request() = default;

    /**
     * Signals that request should be cancelled.
     */
    virtual void cancel() PURE;
  };
  using RequestPtr = std::unique_ptr<Request>;

  /**
   * Notifies caller of the request status.
   */
  class Callbacks {
  public:
    virtual ~Callbacks() = default;

    /**
     * Called when the async request for credentials succeeds.
     */
    virtual void onSuccess() PURE;

    /**
     * Called when the async request for credentials fails.
     */
    virtual void onFailure() PURE;
  };

  /**
   * Request credentials asynchronously
   * @param callbacks the callbacks to be notified of request status.
   * @return a request handle
   */
  virtual RequestPtr requestCredential(Callbacks& callbacks) PURE;

  /**
   * Inject credential to HTTP headers.
   * @param headers supplies the reference to HTTP headers. The credential will be injected into the
   * headers.
   * @param overwrite whether to overwrite the existing credential in the headers.
   *
   * @return true if the credential is injected successfully.
   */
  virtual bool inject(Http::RequestHeaderMap& headers, bool overwrite) PURE;
};

using CredentialInjectorSharedPtr = std::shared_ptr<CredentialInjector>;

} // namespace Common
} // namespace Credentials
} // namespace Extensions
} // namespace Envoy
