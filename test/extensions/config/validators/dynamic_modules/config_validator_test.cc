#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.validate.h"
#include "envoy/extensions/config/validators/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/config/decoded_resource_impl.h"
#include "source/common/config/opaque_resource_decoder_impl.h"
#include "source/common/config/resource_name.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/config/validators/dynamic_modules/config.h"
#include "source/extensions/dynamic_modules/abi/abi.h"

#include "test/mocks/server/instance.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Config {
namespace Validators {
namespace DynamicModules {
namespace {

using DynamicModuleConfigValidatorProto =
    envoy::extensions::config::validators::dynamic_modules::v3::DynamicModuleConfigValidator;

class DynamicModuleConfigValidatorTest : public testing::Test {
public:
  DynamicModuleConfigValidatorTest() {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute(
            "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
        1);
  }

  static DynamicModuleConfigValidatorProto makeProto(absl::string_view module_name,
                                                     absl::string_view extension_name,
                                                     absl::string_view type_url) {
    DynamicModuleConfigValidatorProto proto_config;
    proto_config.mutable_dynamic_module_config()->set_name(std::string(module_name));
    proto_config.mutable_dynamic_module_config()->set_do_not_close(true);
    proto_config.set_extension_name(std::string(extension_name));
    proto_config.set_type_url(std::string(type_url));

    Protobuf::StringValue string_value;
    string_value.set_value("cluster_0");
    std::ignore = proto_config.mutable_extension_config()->PackFrom(string_value);
    return proto_config;
  }

  static DynamicModuleConfigValidatorProto makeProtoFromLocalFilename(absl::string_view filename,
                                                                      absl::string_view type_url) {
    DynamicModuleConfigValidatorProto proto_config;
    proto_config.mutable_dynamic_module_config()->mutable_module()->mutable_local()->set_filename(
        std::string(filename));
    proto_config.mutable_dynamic_module_config()->set_do_not_close(true);
    proto_config.set_extension_name("accept_all");
    proto_config.set_type_url(std::string(type_url));
    return proto_config;
  }

  static DynamicModuleConfigValidatorProto makeProtoFromRemoteSource(absl::string_view type_url) {
    DynamicModuleConfigValidatorProto proto_config;
    auto* remote = proto_config.mutable_dynamic_module_config()->mutable_module()->mutable_remote();
    remote->mutable_http_uri()->set_uri("http://example.com/libconfig_validator_test.so");
    remote->mutable_http_uri()->set_cluster("xds_cluster");
    remote->mutable_http_uri()->mutable_timeout()->set_seconds(1);
    remote->set_sha256("0123456789abcdef");
    proto_config.set_extension_name("accept_all");
    proto_config.set_type_url(std::string(type_url));
    return proto_config;
  }

  static Protobuf::Any pack(const DynamicModuleConfigValidatorProto& proto_config) {
    Protobuf::Any typed_config;
    std::ignore = typed_config.PackFrom(proto_config);
    return typed_config;
  }

  static Envoy::Config::DecodedResourcePtr clusterResource(absl::string_view name,
                                                           absl::string_view version) {
    auto cluster = std::make_unique<envoy::config::cluster::v3::Cluster>();
    cluster->set_name(std::string(name));
    return std::make_unique<Envoy::Config::DecodedResourceImpl>(
        std::move(cluster), std::string(name), std::vector<std::string>{}, std::string(version));
  }

  static Envoy::Config::DecodedResourcePtr payloadlessClusterResource(absl::string_view name,
                                                                      absl::string_view version) {
    Envoy::Config::OpaqueResourceDecoderImpl<envoy::config::cluster::v3::Cluster> decoder(
        ProtobufMessage::getStrictValidationVisitor(), "name");
    envoy::service::discovery::v3::Resource resource;
    resource.set_name(std::string(name));
    resource.set_version(std::string(version));
    return std::make_unique<Envoy::Config::DecodedResourceImpl>(decoder, resource);
  }

  // Builds a resource carrying aliases and a TTL, so tests can prove those envelope fields cross
  // the ABI.
  static Envoy::Config::DecodedResourcePtr envelopeResource(absl::string_view name,
                                                            absl::string_view version) {
    Envoy::Config::OpaqueResourceDecoderImpl<envoy::config::cluster::v3::Cluster> decoder(
        ProtobufMessage::getStrictValidationVisitor(), "name");
    envoy::service::discovery::v3::Resource resource;
    resource.set_name(std::string(name));
    resource.set_version(std::string(version));
    resource.add_aliases("alias_0");
    resource.add_aliases("alias_1");
    resource.mutable_ttl()->set_seconds(5);
    return std::make_unique<Envoy::Config::DecodedResourceImpl>(decoder, resource);
  }

  const std::string cluster_type_url_{
      Envoy::Config::getTypeUrl<envoy::config::cluster::v3::Cluster>()};
  const std::string module_path_{TestEnvironment::substitute(
      "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust/"
      "libconfig_validator_test.so")};
  DynamicModuleConfigValidatorFactory factory_;
  testing::NiceMock<Server::MockInstance> server_;
};

TEST_F(DynamicModuleConfigValidatorTest, FactoryNameAndRegistration) {
  EXPECT_EQ("envoy.config.validators.dynamic_modules", factory_.name());
  auto* registered_factory =
      Registry::FactoryRegistry<Envoy::Config::ConfigValidatorFactory>::getFactory(
          "envoy.config.validators.dynamic_modules");
  EXPECT_NE(nullptr, registered_factory);
}

TEST_F(DynamicModuleConfigValidatorTest, CreateEmptyConfigProto) {
  auto proto = factory_.createEmptyConfigProto();
  EXPECT_NE(nullptr, proto);
  EXPECT_NE(nullptr, dynamic_cast<DynamicModuleConfigValidatorProto*>(proto.get()));
}

TEST_F(DynamicModuleConfigValidatorTest, TypeUrlFromConfig) {
  const auto proto_config =
      makeProto("config_validator_test", "required_clusters", cluster_type_url_);
  EXPECT_EQ(cluster_type_url_,
            factory_.typeUrlFromConfig(pack(proto_config),
                                       ProtobufMessage::getStrictValidationVisitor()));
}

TEST_F(DynamicModuleConfigValidatorTest, InvalidConfigRejected) {
  auto proto_config = makeProto("config_validator_test", "required_clusters", cluster_type_url_);
  proto_config.clear_type_url();

  EXPECT_THROW(factory_.createConfigValidator(pack(proto_config),
                                              ProtobufMessage::getStrictValidationVisitor()),
               EnvoyException);
}

TEST_F(DynamicModuleConfigValidatorTest, NonCanonicalTypeUrlRejected) {
  const auto proto_config =
      makeProto("config_validator_test", "required_clusters", "envoy.config.cluster.v3.Cluster");

  EXPECT_THROW_WITH_REGEX(
      factory_.typeUrlFromConfig(pack(proto_config), ProtobufMessage::getStrictValidationVisitor()),
      EnvoyException, "type_url must use the canonical type.googleapis.com/<message> form");
}

TEST_F(DynamicModuleConfigValidatorTest, EmptyDescriptorTypeUrlRejected) {
  const auto proto_config =
      makeProto("config_validator_test", "required_clusters", "type.googleapis.com/");

  EXPECT_THROW_WITH_REGEX(
      factory_.typeUrlFromConfig(pack(proto_config), ProtobufMessage::getStrictValidationVisitor()),
      EnvoyException, "type_url must use the canonical type.googleapis.com/<message> form");
}

TEST_F(DynamicModuleConfigValidatorTest, ModuleNotFoundRejected) {
  const auto proto_config =
      makeProto("missing_config_validator_module", "required_clusters", cluster_type_url_);

  EXPECT_THROW_WITH_REGEX(factory_.createConfigValidator(
                              pack(proto_config), ProtobufMessage::getStrictValidationVisitor()),
                          EnvoyException, "Failed to load dynamic module");
}

TEST_F(DynamicModuleConfigValidatorTest, LocalFilenameModuleSourceSupported) {
  const auto proto_config = makeProtoFromLocalFilename(module_path_, cluster_type_url_);

  auto validator = factory_.createConfigValidator(pack(proto_config),
                                                  ProtobufMessage::getStrictValidationVisitor());
  std::vector<Envoy::Config::DecodedResourcePtr> resources;

  EXPECT_NO_THROW(validator->validate(server_, resources));
}

TEST_F(DynamicModuleConfigValidatorTest, RemoteModuleSourceRejected) {
  const auto proto_config = makeProtoFromRemoteSource(cluster_type_url_);

  EXPECT_THROW_WITH_REGEX(factory_.createConfigValidator(
                              pack(proto_config), ProtobufMessage::getStrictValidationVisitor()),
                          EnvoyException, "Remote module sources require a factory context");
}

TEST_F(DynamicModuleConfigValidatorTest, MissingSymbolsRejected) {
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
      1);
  const auto proto_config = makeProto("no_op", "required_clusters", cluster_type_url_);

  EXPECT_THROW_WITH_REGEX(factory_.createConfigValidator(
                              pack(proto_config), ProtobufMessage::getStrictValidationVisitor()),
                          EnvoyException, "Failed to create dynamic module config validator");
}

TEST_F(DynamicModuleConfigValidatorTest, InitFailureRejected) {
  const auto proto_config =
      makeProto("config_validator_test", "unknown_validator", cluster_type_url_);

  EXPECT_THROW_WITH_REGEX(factory_.createConfigValidator(
                              pack(proto_config), ProtobufMessage::getStrictValidationVisitor()),
                          EnvoyException, "Failed to create dynamic module config validator");
}

TEST_F(DynamicModuleConfigValidatorTest, SotwValidateSuccess) {
  const auto proto_config =
      makeProto("config_validator_test", "required_clusters", cluster_type_url_);
  auto validator = factory_.createConfigValidator(pack(proto_config),
                                                  ProtobufMessage::getStrictValidationVisitor());

  std::vector<Envoy::Config::DecodedResourcePtr> resources;
  resources.push_back(clusterResource("cluster_0", "version_0"));
  resources.push_back(clusterResource("cluster_1", "version_1"));

  EXPECT_NO_THROW(validator->validate(server_, resources));
}

TEST_F(DynamicModuleConfigValidatorTest, ResourcePayloadPresenceCrossesAbi) {
  const auto proto_config =
      makeProto("config_validator_test", "resource_presence", cluster_type_url_);
  auto validator = factory_.createConfigValidator(pack(proto_config),
                                                  ProtobufMessage::getStrictValidationVisitor());

  std::vector<Envoy::Config::DecodedResourcePtr> resources;
  resources.push_back(clusterResource("cluster_present", "version_0"));
  resources.push_back(payloadlessClusterResource("cluster_absent", "version_1"));
  EXPECT_NO_THROW(validator->validate(server_, resources));

  std::vector<Envoy::Config::DecodedResourcePtr> added_resources;
  added_resources.push_back(clusterResource("cluster_present", "version_0"));
  added_resources.push_back(payloadlessClusterResource("cluster_absent", "version_1"));
  Protobuf::RepeatedPtrField<std::string> removed_resources;
  EXPECT_NO_THROW(validator->validate(server_, added_resources, removed_resources));
}

TEST_F(DynamicModuleConfigValidatorTest, SotwValidateRejection) {
  const auto proto_config =
      makeProto("config_validator_test", "required_clusters", cluster_type_url_);
  auto validator = factory_.createConfigValidator(pack(proto_config),
                                                  ProtobufMessage::getStrictValidationVisitor());

  std::vector<Envoy::Config::DecodedResourcePtr> resources;
  resources.push_back(clusterResource("cluster_1", "version_0"));

  EXPECT_THROW_WITH_REGEX(validator->validate(server_, resources), EnvoyException,
                          "required cluster 'cluster_0' is absent");
}

TEST_F(DynamicModuleConfigValidatorTest, DeltaValidateSuccess) {
  const auto proto_config =
      makeProto("config_validator_test", "required_clusters", cluster_type_url_);
  auto validator = factory_.createConfigValidator(pack(proto_config),
                                                  ProtobufMessage::getStrictValidationVisitor());

  std::vector<Envoy::Config::DecodedResourcePtr> added_resources;
  added_resources.push_back(clusterResource("cluster_1", "version_0"));
  Protobuf::RepeatedPtrField<std::string> removed_resources;
  removed_resources.Add("cluster_1");

  EXPECT_NO_THROW(validator->validate(server_, added_resources, removed_resources));
}

TEST_F(DynamicModuleConfigValidatorTest, EmptyResourceArraysAccepted) {
  const auto proto_config = makeProto("config_validator_test", "accept_all", cluster_type_url_);
  auto validator = factory_.createConfigValidator(pack(proto_config),
                                                  ProtobufMessage::getStrictValidationVisitor());

  std::vector<Envoy::Config::DecodedResourcePtr> resources;
  EXPECT_NO_THROW(validator->validate(server_, resources));

  std::vector<Envoy::Config::DecodedResourcePtr> added_resources;
  Protobuf::RepeatedPtrField<std::string> removed_resources;
  EXPECT_NO_THROW(validator->validate(server_, added_resources, removed_resources));
}

TEST_F(DynamicModuleConfigValidatorTest, DeltaValidateRejectionWithRemovedResourceName) {
  const auto proto_config =
      makeProto("config_validator_test", "required_clusters", cluster_type_url_);
  auto validator = factory_.createConfigValidator(pack(proto_config),
                                                  ProtobufMessage::getStrictValidationVisitor());

  std::vector<Envoy::Config::DecodedResourcePtr> added_resources;
  Protobuf::RepeatedPtrField<std::string> removed_resources;
  removed_resources.Add("cluster_0");

  EXPECT_THROW_WITH_REGEX(validator->validate(server_, added_resources, removed_resources),
                          EnvoyException, "required cluster 'cluster_0' was removed");
}

TEST_F(DynamicModuleConfigValidatorTest, EmptyRejectionMessageUsesGenericError) {
  const auto proto_config =
      makeProto("config_validator_test", "empty_rejection_message", cluster_type_url_);
  auto validator = factory_.createConfigValidator(pack(proto_config),
                                                  ProtobufMessage::getStrictValidationVisitor());

  std::vector<Envoy::Config::DecodedResourcePtr> resources;
  resources.push_back(clusterResource("cluster_0", "version_0"));

  EXPECT_THROW_WITH_MESSAGE(
      validator->validate(server_, resources), EnvoyException,
      absl::StrCat("dynamic module config validator rejected ", cluster_type_url_, " update"));
}

TEST_F(DynamicModuleConfigValidatorTest, PanicRejected) {
  const auto proto_config = makeProto("config_validator_test", "panic", cluster_type_url_);
  auto validator = factory_.createConfigValidator(pack(proto_config),
                                                  ProtobufMessage::getStrictValidationVisitor());

  std::vector<Envoy::Config::DecodedResourcePtr> resources;
  resources.push_back(clusterResource("cluster_0", "version_0"));

  EXPECT_THROW_WITH_REGEX(validator->validate(server_, resources), EnvoyException, "panicked");
}

TEST_F(DynamicModuleConfigValidatorTest, DeltaValidatePanicRejected) {
  const auto proto_config = makeProto("config_validator_test", "panic", cluster_type_url_);
  auto validator = factory_.createConfigValidator(pack(proto_config),
                                                  ProtobufMessage::getStrictValidationVisitor());

  std::vector<Envoy::Config::DecodedResourcePtr> added_resources;
  added_resources.push_back(clusterResource("cluster_0", "version_0"));
  Protobuf::RepeatedPtrField<std::string> removed_resources;

  EXPECT_THROW_WITH_REGEX(validator->validate(server_, added_resources, removed_resources),
                          EnvoyException, "panicked");
}

TEST_F(DynamicModuleConfigValidatorTest, DeltaEmptyRejectionMessageUsesGenericError) {
  const auto proto_config =
      makeProto("config_validator_test", "empty_rejection_message", cluster_type_url_);
  auto validator = factory_.createConfigValidator(pack(proto_config),
                                                  ProtobufMessage::getStrictValidationVisitor());

  std::vector<Envoy::Config::DecodedResourcePtr> added_resources;
  Protobuf::RepeatedPtrField<std::string> removed_resources;

  EXPECT_THROW_WITH_MESSAGE(
      validator->validate(server_, added_resources, removed_resources), EnvoyException,
      absl::StrCat("dynamic module config validator rejected ", cluster_type_url_, " update"));
}

TEST_F(DynamicModuleConfigValidatorTest, TypeUrlWithoutConfigIsEnvoyBug) {
  EXPECT_ENVOY_BUG(factory_.typeUrl(), "requires typed configuration");
}

TEST_F(DynamicModuleConfigValidatorTest, SetRejectionMessageWithNullContextIsEnvoyBug) {
  const envoy_dynamic_module_type_module_buffer rejection_buffer{"reason", 6};
  EXPECT_ENVOY_BUG(envoy_dynamic_module_callback_config_validator_set_rejection_message(
                       nullptr, rejection_buffer),
                   "null context");
}

TEST_F(DynamicModuleConfigValidatorTest, SetRejectionMessageWithNullMessageAndLengthIsEnvoyBug) {
  int placeholder = 0;
  auto* context_envoy_ptr =
      static_cast<envoy_dynamic_module_type_config_validator_context_envoy_ptr>(&placeholder);
  const envoy_dynamic_module_type_module_buffer rejection_buffer{nullptr, 5};
  EXPECT_ENVOY_BUG(envoy_dynamic_module_callback_config_validator_set_rejection_message(
                       context_envoy_ptr, rejection_buffer),
                   "null message with non-zero length");
}

TEST_F(DynamicModuleConfigValidatorTest, EnvelopeAliasesAndTtlCrossAbi) {
  const auto proto_config = makeProto("config_validator_test", "envelope_check", cluster_type_url_);
  auto validator = factory_.createConfigValidator(pack(proto_config),
                                                  ProtobufMessage::getStrictValidationVisitor());

  std::vector<Envoy::Config::DecodedResourcePtr> resources;
  resources.push_back(envelopeResource("cluster_0", "version_0"));
  EXPECT_NO_THROW(validator->validate(server_, resources));

  std::vector<Envoy::Config::DecodedResourcePtr> added_resources;
  added_resources.push_back(envelopeResource("cluster_0", "version_0"));
  Protobuf::RepeatedPtrField<std::string> removed_resources;
  EXPECT_NO_THROW(validator->validate(server_, added_resources, removed_resources));
}

TEST_F(DynamicModuleConfigValidatorTest, GetDynamicClusterCountWithNullContextIsEnvoyBug) {
  EXPECT_ENVOY_BUG(
      envoy_dynamic_module_callback_config_validator_get_dynamic_cluster_count(nullptr),
      "null context");
}

TEST_F(DynamicModuleConfigValidatorTest, ExtensionConfigParseFailureRejected) {
  auto proto_config = makeProto("config_validator_test", "accept_all", cluster_type_url_);
  Protobuf::Any& extension_config = *proto_config.mutable_extension_config();
  extension_config.set_type_url("type.googleapis.com/google.protobuf.StringValue");
  extension_config.set_value("\xff\xff\xff");

  EXPECT_THROW_WITH_REGEX(factory_.createConfigValidator(
                              pack(proto_config), ProtobufMessage::getStrictValidationVisitor()),
                          EnvoyException, "Failed to parse dynamic module config validator");
}

TEST_F(DynamicModuleConfigValidatorTest, MissingExtensionNameRejected) {
  auto proto_config = makeProto("config_validator_test", "required_clusters", cluster_type_url_);
  proto_config.clear_extension_name();

  EXPECT_THROW(factory_.createConfigValidator(pack(proto_config),
                                              ProtobufMessage::getStrictValidationVisitor()),
               EnvoyException);
}

TEST_F(DynamicModuleConfigValidatorTest, MissingDynamicModuleConfigRejected) {
  auto proto_config = makeProto("config_validator_test", "required_clusters", cluster_type_url_);
  proto_config.clear_dynamic_module_config();

  EXPECT_THROW(factory_.createConfigValidator(pack(proto_config),
                                              ProtobufMessage::getStrictValidationVisitor()),
               EnvoyException);
}

} // namespace
} // namespace DynamicModules
} // namespace Validators
} // namespace Config
} // namespace Extensions
} // namespace Envoy
