#pragma once

#include <string>
#include <tuple>

#include "envoy/http/header_map.h"
#include "envoy/http/protocol.h"

namespace Envoy {
namespace Http {

/**
 * Common interface for server and client header validators.
 */
class HeaderValidator {
public:
  virtual ~HeaderValidator() = default;

  // A class that holds either success condition or an error condition with tuple of
  // action and error details.
  template <typename ActionType> class Result {
  public:
    using Action = ActionType;

    // Helper for constructing successful results
    static Result success() { return Result(ActionType::Accept, absl::string_view()); }

    Result(ActionType action, absl::string_view details) : result_(action, details) {
      ENVOY_BUG(action == ActionType::Accept || !details.empty(),
                "Error details must not be empty in case of an error");
    }

    bool ok() const { return std::get<0>(result_) == ActionType::Accept; }
    operator bool() const { return ok(); }
    absl::string_view details() const { return std::get<1>(result_); }
    Action action() const { return std::get<0>(result_); }

  private:
    const std::tuple<ActionType, std::string> result_;
  };

  enum class RejectAction { Accept, Reject };
  enum class RejectOrRedirectAction { Accept, Reject, Redirect };
  using RejectResult = Result<RejectAction>;
  using RejectOrRedirectResult = Result<RejectOrRedirectAction>;
  using TransformationResult = RejectResult;

  /**
   * Validate the entire request header map.
   * Returning the Reject value form this method causes the HTTP request to be rejected with 400
   * status, and the gRPC request with the INTERNAL (13) error code.
   */
  using ValidationResult = RejectResult;
  virtual ValidationResult validateRequestHeaders(const RequestHeaderMap& header_map) PURE;

  /**
   * Validate the entire response header map.
   * Returning the Reject value causes the HTTP request to be rejected with the 502 status,
   * and the gRPC request with the UNAVAILABLE (14) error code.
   */
  virtual ValidationResult validateResponseHeaders(const ResponseHeaderMap& header_map) PURE;

  /**
   * Validate the entire request trailer map.
   * Returning the Reject value causes the HTTP request to be rejected with the 502 status,
   * and the gRPC request with the UNAVAILABLE (14) error code.
   * If response headers have already been sent the request is reset.
   */
  virtual ValidationResult validateRequestTrailers(const RequestTrailerMap& trailer_map) PURE;

  /**
   * Validate the entire response trailer map.
   * Returning the Reject value causes the HTTP request to be reset.
   */
  virtual ValidationResult validateResponseTrailers(const ResponseTrailerMap& trailer_map) PURE;
};

/**
 * Interface for server header validators.
 */
class ServerHeaderValidator : public HeaderValidator {
public:
  ~ServerHeaderValidator() override = default;

  /**
   * Transform the entire request header map.
   * This method transforms the header map, for example by normalizing URI path, before processing
   * by the filter chain.
   * Returning the Reject value from this method causes the HTTP request to be rejected with 400
   * status, and the gRPC request with the INTERNAL (13) error code. Returning the Redirect
   * value causes the HTTP request to be redirected to the :path presudo header in the request map.
   * The gRPC request will still be rejected with the INTERNAL (13) error code.
   */
  using RequestHeadersTransformationResult = RejectOrRedirectResult;
  virtual RequestHeadersTransformationResult
  transformRequestHeaders(RequestHeaderMap& header_map) PURE;

  /**
   * Transform the entire request trailer map.
   * Returning the Reject value causes the HTTP request to be rejected with the 502 status,
   * and the gRPC request with the UNAVAILABLE (14) error code.
   * If response headers have already been sent the request is reset.
   */
  virtual TransformationResult transformRequestTrailers(RequestTrailerMap& header_map) PURE;

  /**
   * Transform the entire response header map.
   * HTTP/2 and HTTP/3 server header validator may transform the HTTP/1 upgrade response
   * to HTTP/2 extended CONNECT response, iff it transformed extended CONNECT to upgrade request
   * during request validation.
   * Returning the Reject value causes the HTTP request to be rejected with the 502 status,
   * and the gRPC request with the UNAVAILABLE (14) error code.
   */
  struct ResponseHeadersTransformationResult {
    static ResponseHeadersTransformationResult success() {
      return ResponseHeadersTransformationResult{RejectResult::success(), nullptr};
    }
    RejectResult status;
    ResponseHeaderMapPtr new_headers;
  };
  virtual ResponseHeadersTransformationResult
  transformResponseHeaders(const ResponseHeaderMap& header_map) PURE;
};

/**
 * Interface for server header validators.
 */
class ClientHeaderValidator : public HeaderValidator {
public:
  ~ClientHeaderValidator() override = default;

  /**
   * Transform the entire request header map.
   * This method can not mutate the header map as it is immutable after the terminal decoder filter.
   * However HTTP/2 and HTTP/3 header validators may need to change the request from the HTTP/1
   * upgrade to to the extended CONNECT. In this case the new header map is returned in the
   * `new_headers` member of the returned structure. Returning the Reject value form this method
   * causes the HTTP request to be rejected with 400 status, and the gRPC request with the INTERNAL
   * (13) error code.
   */
  struct RequestHeadersTransformationResult {
    static RequestHeadersTransformationResult success() {
      return RequestHeadersTransformationResult{RejectResult::success(), nullptr};
    }
    RejectResult status;
    RequestHeaderMapPtr new_headers;
  };
  virtual RequestHeadersTransformationResult
  transformRequestHeaders(const RequestHeaderMap& header_map) PURE;

  /**
   * Transform the entire response header map.
   * HTTP/2 and HTTP/3 client header validator may transform the extended CONNECT response
   * to HTTP/1 upgrade response, iff it transformed upgrade request to extended CONNECT
   * during request validation.
   * Returning the Reject value causes the HTTP request to be rejected with the 502 status,
   * and the gRPC request with the UNAVAILABLE (14) error code.
   */
  virtual TransformationResult transformResponseHeaders(ResponseHeaderMap& header_map) PURE;
};

using ServerHeaderValidatorPtr = std::unique_ptr<ServerHeaderValidator>;
using ClientHeaderValidatorPtr = std::unique_ptr<ClientHeaderValidator>;

/**
 * Interface for stats.
 */
class HeaderValidatorStats {
public:
  virtual ~HeaderValidatorStats() = default;

  virtual void incDroppedHeadersWithUnderscores() PURE;
  virtual void incRequestsRejectedWithUnderscoresInHeaders() PURE;
  virtual void incMessagingError() PURE;
};

/**
 * Interface for creating header validators.
 * TODO(yanavlasov): split into factories dedicated to server and client header validators.
 */
class HeaderValidatorFactory {
public:
  virtual ~HeaderValidatorFactory() = default;

  /**
   * Create a new header validator for the specified protocol.
   */
  virtual ServerHeaderValidatorPtr createServerHeaderValidator(Protocol protocol,
                                                               HeaderValidatorStats& stats) PURE;
  virtual ClientHeaderValidatorPtr createClientHeaderValidator(Protocol protocol,
                                                               HeaderValidatorStats& stats) PURE;
};

using HeaderValidatorFactoryPtr = std::unique_ptr<HeaderValidatorFactory>;

} // namespace Http
} // namespace Envoy
