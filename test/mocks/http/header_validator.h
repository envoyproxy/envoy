#pragma once

#include "envoy/http/header_validator.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Http {

class MockHeaderValidator : public HeaderValidator {
public:
  ~MockHeaderValidator() override = default;
  MOCK_METHOD(ValidationResult, validateRequestHeaders, (const RequestHeaderMap& header_map));
  MOCK_METHOD(HeadersTransformationResult, transformRequestHeaders,
              (RequestHeaderMap & header_map));
  MOCK_METHOD(ValidationResult, validateResponseHeaders, (const ResponseHeaderMap& header_map));
  MOCK_METHOD(ValidationResult, validateRequestTrailers, (const RequestTrailerMap& trailer_map));
  MOCK_METHOD(TrailersTransformationResult, transformRequestTrailers,
              (RequestTrailerMap & trailer_map));
  MOCK_METHOD(ValidationResult, validateResponseTrailers, (const ResponseTrailerMap& trailer_map));
};

class MockHeaderValidatorStats : public HeaderValidatorStats {
public:
  MOCK_METHOD(void, incDroppedHeadersWithUnderscores, ());
  MOCK_METHOD(void, incRequestsRejectedWithUnderscoresInHeaders, ());
  MOCK_METHOD(void, incMessagingError, ());
};

class MockHeaderValidatorFactory : public HeaderValidatorFactory {
public:
  MOCK_METHOD(HeaderValidatorPtr, create, (Protocol protocol, HeaderValidatorStats& stats));
};

} // namespace Http
} // namespace Envoy
