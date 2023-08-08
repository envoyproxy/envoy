#include "source/common/config/custom_config_validators_impl.h"

#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/test_common/registry.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

class FakeConfigValidator : public ConfigValidator {
public:
  FakeConfigValidator(bool should_reject) : should_reject_(should_reject) {}

  // ConfigValidator
  void validate(const Server::Instance&,
                const std::vector<Envoy::Config::DecodedResourcePtr>&) override {
    if (should_reject_) {
      throw EnvoyException("Emulating fake action throw exception (SotW)");
    }
  }

  void validate(const Server::Instance&, const std::vector<Envoy::Config::DecodedResourcePtr>&,
                const Protobuf::RepeatedPtrField<std::string>&) override {
    if (should_reject_) {
      throw EnvoyException("Emulating fake action throw exception (Delta)");
    }
  }

  bool should_reject_;
};

class FakeConfigValidatorFactory : public ConfigValidatorFactory {
public:
  FakeConfigValidatorFactory(bool should_reject) : should_reject_(should_reject) {}

  ConfigValidatorPtr createConfigValidator(const ProtobufWkt::Any&,
                                           ProtobufMessage::ValidationVisitor&) override {
    return std::make_unique<FakeConfigValidator>(should_reject_);
  }

  Envoy::ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom empty config proto. This is only allowed in tests.
    return should_reject_ ? ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()}
                          : ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Value()};
  }

  std::string name() const override {
    return absl::StrCat(category(), ".fake_config_validator_",
                        should_reject_ ? "reject" : "accept");
  }

  std::string typeUrl() const override {
    return Envoy::Config::getTypeUrl<envoy::config::cluster::v3::Cluster>();
  }

  bool should_reject_;
};

class CustomConfigValidatorsImplTest : public testing::Test {
public:
  CustomConfigValidatorsImplTest()
      : factory_accept_(false), factory_reject_(true), register_factory_accept_(factory_accept_),
        register_factory_reject_(factory_reject_) {}

  static envoy::config::core::v3::TypedExtensionConfig parseConfig(const std::string& config) {
    envoy::config::core::v3::TypedExtensionConfig proto;
    TestUtility::loadFromYaml(config, proto);
    return proto;
  }

  FakeConfigValidatorFactory factory_accept_;
  FakeConfigValidatorFactory factory_reject_;
  Registry::InjectFactory<ConfigValidatorFactory> register_factory_accept_;
  Registry::InjectFactory<ConfigValidatorFactory> register_factory_reject_;
  testing::NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  const testing::NiceMock<Server::MockInstance> server_;
  const std::string type_url_{Envoy::Config::getTypeUrl<envoy::config::cluster::v3::Cluster>()};

  static constexpr char AcceptValidatorConfig[] = R"EOF(
      name: envoy.config.validators.fake_config_validator_accept
      typed_config:
        "@type": type.googleapis.com/google.protobuf.Value
  )EOF";
  static constexpr char RejectValidatorConfig[] = R"EOF(
      name: envoy.config.validators.fake_config_validator_reject
      typed_config:
        "@type": type.googleapis.com/google.protobuf.Struct
  )EOF";
};

// Validates that empty config that has no validators is always accepted.
TEST_F(CustomConfigValidatorsImplTest, EmptyConfigValidator) {
  const Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig> empty_list;
  CustomConfigValidatorsImpl validators(validation_visitor_, server_, empty_list);
  {
    const std::vector<DecodedResourcePtr> resources;
    validators.executeValidators(type_url_, resources);
  }
  {
    const std::vector<DecodedResourcePtr> added_resources;
    const Protobuf::RepeatedPtrField<std::string> removed_resources;
    validators.executeValidators(type_url_, added_resources, removed_resources);
  }
}

// Validates that a config that creates a validator that accepts does so.
TEST_F(CustomConfigValidatorsImplTest, AcceptConfigValidator) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig> configs_list;
  auto* entry = configs_list.Add();
  *entry = parseConfig(AcceptValidatorConfig);
  CustomConfigValidatorsImpl validators(validation_visitor_, server_, configs_list);
  {
    const std::vector<DecodedResourcePtr> resources;
    validators.executeValidators(type_url_, resources);
  }
  {
    const std::vector<DecodedResourcePtr> added_resources;
    const Protobuf::RepeatedPtrField<std::string> removed_resources;
    validators.executeValidators(type_url_, added_resources, removed_resources);
  }
}

// Validates that a config that creates a validator that rejects throws an exception.
TEST_F(CustomConfigValidatorsImplTest, RejectConfigValidator) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig> configs_list;
  auto* entry = configs_list.Add();
  *entry = parseConfig(RejectValidatorConfig);
  CustomConfigValidatorsImpl validators(validation_visitor_, server_, configs_list);
  {
    const std::vector<DecodedResourcePtr> resources;
    EXPECT_THROW_WITH_MESSAGE(validators.executeValidators(type_url_, resources), EnvoyException,
                              "Emulating fake action throw exception (SotW)");
  }
  {
    const std::vector<DecodedResourcePtr> added_resources;
    const Protobuf::RepeatedPtrField<std::string> removed_resources;
    EXPECT_THROW_WITH_MESSAGE(
        validators.executeValidators(type_url_, added_resources, removed_resources), EnvoyException,
        "Emulating fake action throw exception (Delta)");
  }
}

// Validates that a config that contains a validator that accepts, followed
// by a validator that rejects, throws an exception.
TEST_F(CustomConfigValidatorsImplTest, AcceptThenRejectConfigValidators) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig> configs_list;
  auto* entry1 = configs_list.Add();
  *entry1 = parseConfig(AcceptValidatorConfig);
  auto* entry2 = configs_list.Add();
  *entry2 = parseConfig(RejectValidatorConfig);
  CustomConfigValidatorsImpl validators(validation_visitor_, server_, configs_list);
  {
    const std::vector<DecodedResourcePtr> resources;
    EXPECT_THROW_WITH_MESSAGE(validators.executeValidators(type_url_, resources), EnvoyException,
                              "Emulating fake action throw exception (SotW)");
  }
  {
    const std::vector<DecodedResourcePtr> added_resources;
    const Protobuf::RepeatedPtrField<std::string> removed_resources;
    EXPECT_THROW_WITH_MESSAGE(
        validators.executeValidators(type_url_, added_resources, removed_resources), EnvoyException,
        "Emulating fake action throw exception (Delta)");
  }
}

// Validates that a config that creates a validator that rejects on
// different type_url isn't rejected.
TEST_F(CustomConfigValidatorsImplTest, ReturnFalseDifferentTypeConfigValidator) {
  const std::string type_url{
      Envoy::Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>()};
  Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig> configs_list;
  auto* entry = configs_list.Add();
  *entry = parseConfig(RejectValidatorConfig);
  CustomConfigValidatorsImpl validators(validation_visitor_, server_, configs_list);
  {
    const std::vector<DecodedResourcePtr> resources;
    validators.executeValidators(type_url, resources);
  }
  {
    const std::vector<DecodedResourcePtr> added_resources;
    const Protobuf::RepeatedPtrField<std::string> removed_resources;
    validators.executeValidators(type_url, added_resources, removed_resources);
  }
}

} // namespace
} // namespace Config
} // namespace Envoy
