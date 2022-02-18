#pragma once

#include "source/common/config/external_config_validators.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Config {

class MockConfigValidator : public ConfigValidator {
public:
  MockConfigValidator();
  ~MockConfigValidator() override;

  MOCK_METHOD(bool, validate,
              (Server::Instance & server, const std::vector<DecodedResourcePtr>& resources));

  MOCK_METHOD(bool, validate,
              (Server::Instance & server, const std::vector<DecodedResourcePtr>& added_resources,
               const Protobuf::RepeatedPtrField<std::string>& removed_resources));
};

class MockExternalConfigValidators : public ExternalConfigValidators {
public:
  MockExternalConfigValidators();
  ~MockExternalConfigValidators() override;

  MOCK_METHOD(void, executeValidators,
              (absl::string_view type_url, const std::vector<DecodedResourcePtr>& resources));

  MOCK_METHOD(void, executeValidators,
              (absl::string_view type_url, const std::vector<DecodedResourcePtr>& added_resources,
               const Protobuf::RepeatedPtrField<std::string>& removed_resources));
};

} // namespace Config
} // namespace Envoy
