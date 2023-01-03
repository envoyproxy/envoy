#pragma once

#include "envoy/http/header_validator.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Http {

class MockHeaderValidator : public HeaderValidator {
public:
  ~MockHeaderValidator() override = default;
  MOCK_METHOD(HeaderEntryValidationResult, validateRequestHeaderEntry,
              (const HeaderString& key, const HeaderString& value));
  MOCK_METHOD(HeaderEntryValidationResult, validateResponseHeaderEntry,
              (const HeaderString& key, const HeaderString& value));
  MOCK_METHOD(RequestHeaderMapValidationResult, validateRequestHeaderMap,
              (RequestHeaderMap & header_map));
  MOCK_METHOD(ResponseHeaderMapValidationResult, validateResponseHeaderMap,
              (ResponseHeaderMap & header_map));
};

class MockHeaderValidatorStats : public HeaderValidatorStats {
public:
  MOCK_METHOD(void, incDroppedHeadersWithUnderscores, ());
  MOCK_METHOD(void, incRequestsRejectedWithUnderscoresInHeaders, ());
};

class MockHeaderValidatorFactory : public HeaderValidatorFactory {
public:
  MOCK_METHOD(HeaderValidatorPtr, create, (Protocol protocol, HeaderValidatorStats& stats));
};

} // namespace Http
} // namespace Envoy
