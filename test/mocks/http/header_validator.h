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

class MockHeaderValidatorFactory : public HeaderValidatorFactory {
public:
  MOCK_METHOD(HeaderValidatorPtr, create, (Protocol protocol, StreamInfo::StreamInfo& stream_info));
};

} // namespace Http
} // namespace Envoy
