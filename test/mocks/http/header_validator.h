#pragma once

#include "envoy/http/header_validator.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Http {

class MockHeaderValidator : public HeaderValidator {
public:
  ~MockHeaderValidator() override = default;
  MOCK_METHOD(ValidationResult, validateRequestHeaderEntry,
              (const HeaderString& key, const HeaderString& value));
  MOCK_METHOD(ValidationResult, validateResponseHeaderEntry,
              (const HeaderString& key, const HeaderString& value));
  MOCK_METHOD(RequestHeaderMapValidationResult, validateRequestHeaderMap,
              (RequestHeaderMap & header_map));
  MOCK_METHOD(ValidationResult, validateResponseHeaderMap, (ResponseHeaderMap & header_map));
  MOCK_METHOD(ValidationResult, validateRequestTrailerMap, (RequestTrailerMap & trailer_map));
  MOCK_METHOD(ValidationResult, validateResponseTrailerMap, (ResponseTrailerMap & trailer_map));
};

class MockHeaderValidatorFactory : public HeaderValidatorFactory {
public:
  MOCK_METHOD(HeaderValidatorPtr, create, (Protocol protocol, StreamInfo::StreamInfo& stream_info));
};

} // namespace Http
} // namespace Envoy
