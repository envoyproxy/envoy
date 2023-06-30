#pragma once

#include "envoy/config/config_validator.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Config {

class MockConfigValidator : public ConfigValidator {
public:
  MockConfigValidator();
  ~MockConfigValidator() override;

  MOCK_METHOD(void, validate,
              (const Server::Instance& server, const std::vector<DecodedResourcePtr>& resources));

  MOCK_METHOD(void, validate,
              (const Server::Instance& server,
               const std::vector<DecodedResourcePtr>& added_resources,
               const Protobuf::RepeatedPtrField<std::string>& removed_resources));
};

class MockCustomConfigValidators : public CustomConfigValidators {
public:
  MockCustomConfigValidators();
  ~MockCustomConfigValidators() override;

  MOCK_METHOD(void, executeValidators,
              (absl::string_view type_url, const std::vector<DecodedResourcePtr>& resources));

  MOCK_METHOD(void, executeValidators,
              (absl::string_view type_url, const std::vector<DecodedResourcePtr>& added_resources,
               const Protobuf::RepeatedPtrField<std::string>& removed_resources));
};

} // namespace Config
} // namespace Envoy
