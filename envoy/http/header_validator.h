#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/http/header_map.h"
#include "envoy/server/factory_context.h"
#include "envoy/stream_info/stream_info.h"

namespace Envoy {
namespace Http {

/**
 * Interface for header key formatters that are stateful. A formatter is created during decoding
 * headers, attached to the header map, and can then be used during encoding for reverse
 * translations if applicable.
 */
class HeaderValidator {
public:
  virtual ~HeaderValidator() = default;

  enum class HeaderEntryValidationResult { Accept, Reject };

  /**
   * Method for validating a request header entry.
   * Returning the Reject value causes the request to be rejected with the 400 status.
   */
  virtual HeaderEntryValidationResult validateRequestHeaderEntry(const HeaderString& key,
                                                                 const HeaderString& value) PURE;

  /**
   * Method for validating a response header entry.
   * Returning the Reject value causes the downstream request to be rejected with the 502 status.
   */
  virtual HeaderEntryValidationResult validateResponseHeaderEntry(const HeaderString& key,
                                                                  const HeaderString& value) PURE;

  /**
   * Validate the entire request header map.
   * This method may mutate the header map as well, for example by normalizing URI path.
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
   * Returning the Reject value causes HTTP requests to be rejected with the 502 status,
   * and gRPC requests with the the UNAVAILABLE (14) error code.
   */
  enum class ResponseHeaderMapValidationResult { Accept, Reject };
  virtual ResponseHeaderMapValidationResult
  validateResponseHeaderMap(ResponseHeaderMap& header_map) PURE;
};

using HeaderValidatorPtr = std::unique_ptr<HeaderValidator>;

/**
 * Interface for creating header validators.
 */
class HeaderValidatorFactory {
public:
  virtual ~HeaderValidatorFactory() = default;

  enum class Protocol { HTTP09, HTTP1, HTTP2, HTTP3 };

  /**
   * Create a new header validator for the specified protocol.
   */
  virtual HeaderValidatorPtr create(Protocol protocol, StreamInfo::StreamInfo& stream_info) PURE;
};

using HeaderValidatorFactorySharedPtr = std::shared_ptr<HeaderValidatorFactory>;

/**
 * Extension configuration for header validators.
 */
class HeaderValidatorFactoryConfig : public Config::TypedFactory {
public:
  virtual HeaderValidatorFactorySharedPtr
  createFromProto(const Protobuf::Message& config,
                  Server::Configuration::FactoryContext& context) PURE;

  std::string category() const override { return "envoy.http.header_validators"; }
};

} // namespace Http
} // namespace Envoy
