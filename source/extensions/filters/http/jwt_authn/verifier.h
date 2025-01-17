#pragma once

#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"

#include "source/extensions/filters/http/jwt_authn/authenticator.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

class Verifier;
using VerifierConstPtr = std::unique_ptr<const Verifier>;

/**
 * Supports verification of JWTs with configured requirements.
 */
class Verifier {
public:
  virtual ~Verifier() = default;

  /**
   * Handle for notifying Verifier callers of request completion.
   */
  class Callbacks {
  public:
    virtual ~Callbacks() = default;

    /**
     * Successfully verified JWT payload are stored in the struct with its
     * *fields* containing **issuer** as keys and **payload** as string values
     * This function is called before onComplete() function.
     * It will not be called if no payload to write.
     */
    virtual void setExtractedData(const ProtobufWkt::Struct& payload) PURE;

    /**
     * JWT payloads added to headers may require clearing the cached route.
     */
    virtual void clearRouteCache() PURE;

    /**
     * Called on completion of request.
     *
     * @param status the status of the request.
     */
    virtual void onComplete(const ::google::jwt_verify::Status& status) PURE;
  };

  // Context object to hold data needed for verifier.
  class Context {
  public:
    virtual ~Context() = default;

    /**
     * Returns the request headers wrapped in this context.
     *
     * @return the request headers.
     */
    virtual Http::RequestHeaderMap& headers() const PURE;

    /**
     * Returns the active span wrapped in this context.
     *
     * @return the active span.
     */
    virtual Tracing::Span& parentSpan() const PURE;

    /**
     * Returns the request callback wrapped in this context.
     *
     * @returns the request callback.
     */
    virtual Callbacks* callback() const PURE;

    /**
     * Cancel any pending requests for this context.
     */
    virtual void cancel() PURE;
  };

  using ContextSharedPtr = std::shared_ptr<Context>;

  // Verify all tokens on headers, and signal the caller with callback.
  virtual void verify(ContextSharedPtr context) const PURE;

  // Factory method for creating verifiers.
  static VerifierConstPtr create(
      const envoy::extensions::filters::http::jwt_authn::v3::JwtRequirement& requirement,
      const Protobuf::Map<std::string,
                          envoy::extensions::filters::http::jwt_authn::v3::JwtProvider>& providers,
      const AuthFactory& factory);

  // Factory method for creating verifier contexts.
  static ContextSharedPtr createContext(Http::RequestHeaderMap& headers, Tracing::Span& parent_span,
                                        Callbacks* callback);
};

using ContextSharedPtr = std::shared_ptr<Verifier::Context>;

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
