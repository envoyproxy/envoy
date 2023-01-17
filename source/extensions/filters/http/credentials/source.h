#pragma once

#include <memory>

#include "source/common/config/datasource.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Credentials {

/**
 * Interface for a credential source.
 */
class CredentialSource {
public:
  virtual ~CredentialSource() = default;

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
     * @param credential credential
     */
    virtual void onSuccess(std::string credential) PURE;

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
};
using CredentialSourcePtr = std::unique_ptr<CredentialSource>;

} // namespace Credentials
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
