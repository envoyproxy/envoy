#pragma once

#include "envoy/http/header_validator.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Http {

class MockServerHeaderValidator : public ServerHeaderValidator {
public:
  ~MockServerHeaderValidator() override = default;
  MOCK_METHOD(ValidationResult, validateRequestHeaders, (const RequestHeaderMap& header_map),
              (override));
  MOCK_METHOD(ValidationResult, validateResponseHeaders, (const ResponseHeaderMap& header_map),
              (override));
  MOCK_METHOD(ValidationResult, validateRequestTrailers, (const RequestTrailerMap& header_map),
              (override));
  MOCK_METHOD(ValidationResult, validateResponseTrailers, (const ResponseTrailerMap& header_map),
              (override));
  MOCK_METHOD(RequestHeadersTransformationResult, transformRequestHeaders,
              (RequestHeaderMap & header_map), (override));
  MOCK_METHOD(ResponseHeadersTransformationResult, transformResponseHeaders,
              (const ResponseHeaderMap& header_map), (override));
  MOCK_METHOD(TransformationResult, transformRequestTrailers, (RequestTrailerMap & trailer_map),
              (override));
};

class MockClientHeaderValidator : public ClientHeaderValidator {
public:
  ~MockClientHeaderValidator() override = default;
  MOCK_METHOD(ValidationResult, validateRequestHeaders, (const RequestHeaderMap& header_map),
              (override));
  MOCK_METHOD(ValidationResult, validateResponseHeaders, (const ResponseHeaderMap& header_map),
              (override));
  MOCK_METHOD(ValidationResult, validateRequestTrailers, (const RequestTrailerMap& header_map),
              (override));
  MOCK_METHOD(ValidationResult, validateResponseTrailers, (const ResponseTrailerMap& header_map),
              (override));
  MOCK_METHOD(RequestHeadersTransformationResult, transformRequestHeaders,
              (const RequestHeaderMap& header_map), (override));
  MOCK_METHOD(TransformationResult, transformResponseHeaders, (ResponseHeaderMap & header_map),
              (override));
};

class MockHeaderValidatorStats : public HeaderValidatorStats {
public:
  MOCK_METHOD(void, incDroppedHeadersWithUnderscores, (), (override));
  MOCK_METHOD(void, incRequestsRejectedWithUnderscoresInHeaders, (), (override));
  MOCK_METHOD(void, incMessagingError, (), (override));
};

class MockHeaderValidatorFactory : public HeaderValidatorFactory {
public:
  MOCK_METHOD(ServerHeaderValidatorPtr, createServerHeaderValidator,
              (Protocol protocol, HeaderValidatorStats& stats), (override));
  MOCK_METHOD(ClientHeaderValidatorPtr, createClientHeaderValidator,
              (Protocol protocol, HeaderValidatorStats& stats), (override));
};

} // namespace Http
} // namespace Envoy
