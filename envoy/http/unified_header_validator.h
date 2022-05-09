#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/http/header_map.h"
#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Http {

/**
 * Interface for header key formatters that are stateful. A formatter is created during decoding
 * headers, attached to the header map, and can then be used during encoding for reverse
 * translations if applicable.
 */
class UnifiedHeaderValidator {
public:
  virtual ~UnifiedHeaderValidator() = default;

  enum class HeaderEntryValidationResult { Accept, Reject };

  /**
   * Method for validating a header entry. This is called for both request and response headers.
   * Returning the Reject value causes the request to be rejected with the 400 status.
   */
  virtual HeaderEntryValidationResult validateHeaderEntry(const HeaderString& key,
                                                          const HeaderString& value) PURE;

  /**
   * Validate the entire request header map.
   * This method may mutate header map as well, for example by normalizing URI path.
   * Returning the Reject value form this method causes HTTP requests to be rejected with 400
   * status, and gRPC requests with the the INTERNAL (13) error code. Returning the Redirect value
   * causes HTTP requests to be redirected to the :path presudo header in the request map. gRPC
   * requests will still be rejected with the INTERNAL (13) error code.
   */
  enum class RequestHeaderMapValidationResult { Accept, Reject, Redirect };
  virtual RequestHeaderMapValidationResult
  validateRequestHeaderMap(RequestHeaderMap& header_map) PURE;

  /**
   * Validate the entire response header map.
   * Returning the Reject value causes the request to be rejected with the 502 status.
   */
  enum class ResponseHeaderMapValidationResult { Accept, Reject };
  virtual ResponseHeaderMapValidationResult
  validateResponseHeaderMap(ResponseHeaderMap& header_map) PURE;
};

using UnifiedHeaderValidatorPtr = std::unique_ptr<UnifiedHeaderValidator>;

/**
 * Interface for creating unified header validators.
 */
class UnifiedHeaderValidatorFactory {
public:
  virtual ~UnifiedHeaderValidatorFactory() = default;

  enum class Protocol { HTTP09, HTTP1, HTTP2, HTTP3 };

  /**
   * Create a new unified header validator for the specified protocol.
   */
  virtual UnifiedHeaderValidatorPtr create(Protocol protocol) PURE;
};

using UnifiedHeaderValidatorFactorySharedPtr = std::shared_ptr<UnifiedHeaderValidatorFactory>;

/**
 * Extension configuration for unified header validators.
 */
class UnifiedHeaderValidatorFactoryConfig : public Config::TypedFactory {
public:
  virtual UnifiedHeaderValidatorFactorySharedPtr
  createFromProto(const Protobuf::Message& config,
                  Server::Configuration::FactoryContext& context) PURE;

  std::string category() const override { return "envoy.http.unified_header_validators"; }
};

} // namespace Http
} // namespace Envoy
