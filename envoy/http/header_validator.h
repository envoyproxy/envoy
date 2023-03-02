#pragma once

#include <string>
#include <tuple>

#include "envoy/config/typed_config.h"
#include "envoy/http/header_map.h"
#include "envoy/http/protocol.h"
#include "envoy/protobuf/message_validator.h"

namespace Envoy {
namespace Http {

/**
 * Common interface for server and client header validators.
 * TODO(yanavlasov): rename interfaces in the next PR to `HeaderValidator`
 */
class HeaderValidatorBase {
public:
  virtual ~HeaderValidatorBase() = default;

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

    template <typename OtherActionType> operator Result<OtherActionType>() const {
      // Cast to another result, treating any non-success action as "ActionType::Reject"
      if (std::get<0>(result_) != ActionType::Accept) {
        return Result<OtherActionType>(OtherActionType::Reject, std::get<1>(result_));
      }

      return Result<OtherActionType>::success();
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
  enum class RejectOrDropHeaderAction { Accept, Reject, DropHeader };
  using RejectResult = Result<RejectAction>;
  using RejectOrRedirectResult = Result<RejectOrRedirectAction>;
  using RejectOrDropHeaderResult = Result<RejectOrDropHeaderAction>;

  /**
   * Method for validating a request header entry.
   * Returning the Reject value causes the request to be rejected with the 400 status.
   */
  using HeaderEntryValidationResult = RejectOrDropHeaderResult;
  virtual HeaderEntryValidationResult validateRequestHeaderEntry(const HeaderString& key,
                                                                 const HeaderString& value) PURE;

  /**
   * Method for validating a response header entry.
   * Returning the Reject value causes the downstream request to be rejected with the 502 status.
   */
  virtual HeaderEntryValidationResult validateResponseHeaderEntry(const HeaderString& key,
                                                                  const HeaderString& value) PURE;

  using RequestHeaderMapValidationResult = RejectOrRedirectResult;
  using ResponseHeaderMapValidationResult = RejectResult;
  using TrailerValidationResult = RejectResult;
};

/**
 * Interface for server header validators.
 * TODO(yanavlasov): rename interfaces in the next PR to `ServerHeaderValidator`
 */
class HeaderValidator : public HeaderValidatorBase {
public:
  virtual ~HeaderValidator() = default;

  /**
   * Validate the entire request header map.
   * This method may mutate the header map as well, for example by normalizing URI path.
   * HTTP/2 and HTTP/3 server header validator also transforms extended CONNECT requests
   * to HTTP/1 upgrade requests.
   * Returning the Reject value from this method causes the HTTP request to be rejected with 400
   * status, and the gRPC request with the INTERNAL (13) error code. Returning the Redirect
   * value causes the HTTP request to be redirected to the :path presudo header in the request map.
   * The gRPC request will still be rejected with the INTERNAL (13) error code.
   */
  virtual RequestHeaderMapValidationResult
  validateRequestHeaderMap(RequestHeaderMap& header_map) PURE;

  /**
   * Validate the entire response header map.
   * The header map can not be modified in-place as it is immutable after the terminal encoder
   * filter. However HTTP/2 and HTTP/3 header validators may need to change the status of the
   * upgrade upstream response to the extended CONNECT downstream response. In this case the new
   * header map is returned in the `new_headers` member of the returned structure. Returning the
   * Reject value in the status member causes the HTTP request to be rejected with the 502 status,
   * and the gRPC request with the UNAVAILABLE (14) error code.
   */
  struct ConstResponseHeaderMapValidationResult {
    RejectResult status;
    ResponseHeaderMapPtr new_headers;
  };
  virtual ConstResponseHeaderMapValidationResult
  validateResponseHeaderMap(const ResponseHeaderMap& header_map) PURE;

  /**
   * Validate the entire request trailer map.
   * Returning the Reject value causes the HTTP request to be rejected with the 502 status,
   * and the gRPC request with the UNAVAILABLE (14) error code.
   * If response headers have already been sent the request is reset.
   */
  virtual TrailerValidationResult validateRequestTrailerMap(RequestTrailerMap& trailer_map) PURE;

  /**
   * Note: Response trailers are not validated after they were processed by the encoder filter
   * chain.
   */
};

/**
 * Interface for server header validators.
 */
class ClientHeaderValidator : public HeaderValidatorBase {
public:
  virtual ~ClientHeaderValidator() = default;

  /**
   * Validate the entire request header map.
   * This method can not mutate the header map as it is immutable after the terminal decoder filter.
   * However HTTP/2 and HTTP/3 header validators may need to change the request from the HTTP/1
   * upgrade to to the extended CONNECT. In this case the new header map is returned in the
   * `new_headers` member of the returned structure. Returning the Reject value form this method
   * causes the HTTP request to be rejected with 400 status, and the gRPC request with the INTERNAL
   * (13) error code.
   */
  struct ConstRequestHeaderMapValidationResult {
    RejectResult status;
    RequestHeaderMapPtr new_headers;
  };
  virtual ConstRequestHeaderMapValidationResult
  validateRequestHeaderMap(const RequestHeaderMap& header_map) PURE;

  /**
   * Validate the entire response header map.
   * HTTP/2 and HTTP/3 server header validator may transform the extended CONNECT response
   * to HTTP/1 upgrade response, iff it transformed upgrade request to extended CONNECT
   * during request validation.
   * Returning the Reject value causes the HTTP request to be rejected with the 502 status,
   * and the gRPC request with the UNAVAILABLE (14) error code.
   */
  virtual ResponseHeaderMapValidationResult
  validateResponseHeaderMap(ResponseHeaderMap& header_map) PURE;

  /**
   * Note: Request trailers are not validated after they were processed by the decoder filter chain.
   */

  /**
   * Validate the entire response trailer map.
   * Returning the Reject value causes the HTTP request to be reset.
   */
  virtual TrailerValidationResult validateResponseTrailerMap(ResponseTrailerMap& trailer_map) PURE;
};

using HeaderValidatorPtr = std::unique_ptr<HeaderValidator>;
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
  virtual HeaderValidatorPtr createServerHeaderValidator(Protocol protocol,
                                                         HeaderValidatorStats& stats) PURE;
  virtual ClientHeaderValidatorPtr createClientHeaderValidator(Protocol protocol,
                                                               HeaderValidatorStats& stats) PURE;
};

using HeaderValidatorFactoryPtr = std::unique_ptr<HeaderValidatorFactory>;

/**
 * Extension configuration for header validators.
 */
class HeaderValidatorFactoryConfig : public Config::TypedFactory {
public:
  virtual HeaderValidatorFactoryPtr
  createFromProto(const Protobuf::Message& config,
                  ProtobufMessage::ValidationVisitor& validation_visitor) PURE;

  std::string category() const override { return "envoy.http.header_validators"; }
};

} // namespace Http
} // namespace Envoy
