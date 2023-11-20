#pragma once

#include "envoy/common/exception.h"
#include "envoy/http/header_map.h"

namespace Envoy {
namespace Extensions {
namespace InjectedCredentials {
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
     * @param reason supplies the failure reason.
     */
    virtual void onFailure(const std::string& reason) PURE;
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
   * @return Status whether the injection is successful.
   */
  virtual absl::Status inject(Http::RequestHeaderMap& headers, bool overwrite) PURE;
};

using CredentialInjectorSharedPtr = std::shared_ptr<CredentialInjector>;

} // namespace Common
} // namespace InjectedCredentials
} // namespace Extensions
} // namespace Envoy
