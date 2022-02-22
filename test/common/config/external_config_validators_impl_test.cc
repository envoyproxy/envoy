#include "source/common/config/external_config_validators_impl.h"

#include "test/test_common/registry.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/instance.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

enum class FakeValidatorAction {
  ReturnTrue,
  ReturnFalse,
  ThrowException
};

class FakeConfigValidator : public ConfigValidator {
public:
  FakeConfigValidator(FakeValidatorAction action) : action_(action) {}

  static bool emulateAction(FakeValidatorAction action) {
    switch (action) {
    case FakeValidatorAction::ReturnTrue:
      return true;
    case FakeValidatorAction::ReturnFalse:
      return false;
    case FakeValidatorAction::ThrowException:
      throw EnvoyException("Emulating fake action throw exception");
    }
  }

  // ConfigValidator
  bool validate(Server::Instance&,
                const std::vector<Envoy::Config::DecodedResourcePtr>&) override {
    return emulateAction(action_);
  }

  bool validate(Server::Instance&,
                const std::vector<Envoy::Config::DecodedResourcePtr>&,
                const Protobuf::RepeatedPtrField<std::string>&) override {
    return emulateAction(action_);
  }

  FakeValidatorAction action_;
};

class FakeConfigValidatorFactory : public ConfigValidatorFactory {
public:
  FakeConfigValidatorFactory(FakeValidatorAction action) : action_(action) {}

  ConfigValidatorPtr
  createConfigValidator(const ProtobufWkt::Any&,
                        ProtobufMessage::ValidationVisitor&) override {
    return std::make_unique<FakeConfigValidator>(action_);
  }

  Envoy::ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom empty config proto. This is only allowed in tests.
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }

  std::string name() const override {
    return absl::StrCat(category(), ".fake_config_validator_", enumToInt(action_));
  }

  std::string typeUrl() const override {
    return Envoy::Config::getTypeUrl<envoy::config::cluster::v3::Cluster>();
  }

  FakeValidatorAction action_;
};

class ExternalConfigValidatorsImplTest : public testing::Test {
public:
  ExternalConfigValidatorsImplTest() :
    factory_true_(FakeValidatorAction::ReturnTrue),
    factory_false_(FakeValidatorAction::ReturnFalse),
    factory_throw_(FakeValidatorAction::ThrowException),
    register_factory_true_(factory_true_),
    register_factory_false_(factory_false_),
    register_factory_throw_(factory_throw_) {}

  static envoy::config::core::v3::TypedExtensionConfig parseConfig(const std::string& config) {
    envoy::config::core::v3::TypedExtensionConfig proto;
    TestUtility::loadFromYaml(config, proto);
    return proto;
  }

  FakeConfigValidatorFactory factory_true_;
  FakeConfigValidatorFactory factory_false_;
  FakeConfigValidatorFactory factory_throw_;
  Registry::InjectFactory<ConfigValidatorFactory> register_factory_true_;
  Registry::InjectFactory<ConfigValidatorFactory> register_factory_false_;
  Registry::InjectFactory<ConfigValidatorFactory> register_factory_throw_;
  testing::NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  testing::NiceMock<Server::MockInstance> server_;
  const std::string type_url_{Envoy::Config::getTypeUrl<envoy::config::cluster::v3::Cluster>()};

  static constexpr char returnTrueValidatorConfig[] = "name: envoy.config.validators.fake_config_validator_0";
  static constexpr char returnFalseValidatorConfig[] = "name: envoy.config.validators.fake_config_validator_1";
  static constexpr char throwExceptionValidatorConfig[] = "name: envoy.config.validators.fake_config_validator_2";
};

// Validates that empty config that has no validators always returns true.
TEST_F(ExternalConfigValidatorsImplTest, EmptyConfigValidator) {
  const Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig> empty_list;
  ExternalConfigValidatorsImpl validators(validation_visitor_, server_, empty_list);
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

// Validates that a config that creates a validator that returns true does so.
TEST_F(ExternalConfigValidatorsImplTest, ReturnTrueConfigValidator) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig> configs_list;
  auto* entry = configs_list.Add();
  *entry = parseConfig(returnTrueValidatorConfig);
  ExternalConfigValidatorsImpl validators(validation_visitor_, server_, configs_list);
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

// Validates that a config that creates a validator that returns false throws an
// exception.
TEST_F(ExternalConfigValidatorsImplTest, ReturnFalseConfigValidator) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig> configs_list;
  auto* entry = configs_list.Add();
  *entry = parseConfig(returnFalseValidatorConfig);
  ExternalConfigValidatorsImpl validators(validation_visitor_, server_, configs_list);
  {
    const std::vector<DecodedResourcePtr> resources;
    EXPECT_THROW_WITH_MESSAGE(validators.executeValidators(type_url_, resources), EnvoyException, "External validator rejected the config.");
  }
  {
    const std::vector<DecodedResourcePtr> added_resources;
    const Protobuf::RepeatedPtrField<std::string> removed_resources;
    EXPECT_THROW_WITH_MESSAGE(validators.executeValidators(type_url_, added_resources, removed_resources), EnvoyException, "External validator rejected the config.");
  }
}

// Validates that a config that creates a validator that throws an
// exception, passes that exception out.
TEST_F(ExternalConfigValidatorsImplTest, ThrowExceptionConfigValidator) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig> configs_list;
  auto* entry = configs_list.Add();
  *entry = parseConfig(throwExceptionValidatorConfig);
  ExternalConfigValidatorsImpl validators(validation_visitor_, server_, configs_list);
  {
    const std::vector<DecodedResourcePtr> resources;
    EXPECT_THROW_WITH_MESSAGE(validators.executeValidators(type_url_, resources), EnvoyException, "Emulating fake action throw exception");
  }
  {
    const std::vector<DecodedResourcePtr> added_resources;
    const Protobuf::RepeatedPtrField<std::string> removed_resources;
    EXPECT_THROW_WITH_MESSAGE(validators.executeValidators(type_url_, added_resources, removed_resources), EnvoyException, "Emulating fake action throw exception");
  }
}

// Validates that a config that contains a validator that returns true, followed
// by a validator that returns false, throws an exception.
TEST_F(ExternalConfigValidatorsImplTest, ReturnTrueThenFalseConfigValidator) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig> configs_list;
  auto* entry1 = configs_list.Add();
  *entry1 = parseConfig(returnTrueValidatorConfig);
  auto* entry2 = configs_list.Add();
  *entry2 = parseConfig(returnFalseValidatorConfig);
  ExternalConfigValidatorsImpl validators(validation_visitor_, server_, configs_list);
  {
    const std::vector<DecodedResourcePtr> resources;
    EXPECT_THROW_WITH_MESSAGE(validators.executeValidators(type_url_, resources), EnvoyException, "External validator rejected the config.");
  }
  {
    const std::vector<DecodedResourcePtr> added_resources;
    const Protobuf::RepeatedPtrField<std::string> removed_resources;
    EXPECT_THROW_WITH_MESSAGE(validators.executeValidators(type_url_, added_resources, removed_resources), EnvoyException, "External validator rejected the config.");
  }
}

// Validates that a config that creates a validator that returns false on
// different type_url isn't rejected.
TEST_F(ExternalConfigValidatorsImplTest, ReturnFalseDifferentTypeConfigValidator) {
  const std::string type_url{Envoy::Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>()};
  Protobuf::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig> configs_list;
  auto* entry = configs_list.Add();
  *entry = parseConfig(returnFalseValidatorConfig);
  ExternalConfigValidatorsImpl validators(validation_visitor_, server_, configs_list);
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
