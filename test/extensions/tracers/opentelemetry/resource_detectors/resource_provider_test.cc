#include <string>

#include "envoy/registry/registry.h"

#include "source/extensions/tracers/opentelemetry/resource_detectors/resource_provider.h"

#include "test/mocks/server/tracer_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::Return;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {
namespace {

class SampleDetector : public ResourceDetector {
public:
  MOCK_METHOD(Resource, detect, ());
};

class UntypedDetectorFactory : public ResourceDetectorFactory {
public:
  MOCK_METHOD(ResourceDetectorPtr, createResourceDetector,
              (Server::Configuration::TracerFactoryContext & context));

  std::string name() const override {
    return "envoy.tracers.opentelemetry.resource_detectors.untyped";
  }
};

class TypedDetectorFactory : public ResourceDetectorTypedFactory {
public:
  MOCK_METHOD(ResourceDetectorPtr, createTypedResourceDetector,
              (const Protobuf::Message& message,
               Server::Configuration::TracerFactoryContext& context));

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::Struct>();
  }

  std::string name() const override {
    return "envoy.tracers.opentelemetry.resource_detectors.typed";
  }
};

const std::string kOtelResourceAttributesEnv = "OTEL_RESOURCE_ATTRIBUTES";

class ResourceProviderTest : public testing::Test {
public:
  ResourceProviderTest() {
    resource_untyped_.attributes.insert(std::pair<std::string, std::string>("key1", "val1"));
    resource_typed_.attributes.insert(std::pair<std::string, std::string>("key2", "val2"));
  }
  NiceMock<Server::Configuration::MockTracerFactoryContext> context_;
  Resource resource_untyped_;
  Resource resource_typed_;
};

TEST_F(ResourceProviderTest, NoResourceDetectorsConfigured) {
  const std::string yaml_string = R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: fake-cluster
      timeout: 0.250s
    service_name: my-service
    )EOF";
  envoy::config::trace::v3::OpenTelemetryConfig opentelemetry_config;
  TestUtility::loadFromYaml(yaml_string, opentelemetry_config);

  ResourceProviderImpl resource_provider;
  Resource resource = resource_provider.getResource(opentelemetry_config, context_);

  EXPECT_EQ(resource.schemaUrl, "");

  // Only the service name was added to the resource
  EXPECT_EQ(1, resource.attributes.size());
}

TEST_F(ResourceProviderTest, ServiceNameNotProvided) {
  const std::string yaml_string = R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: fake-cluster
      timeout: 0.250s
    )EOF";
  envoy::config::trace::v3::OpenTelemetryConfig opentelemetry_config;
  TestUtility::loadFromYaml(yaml_string, opentelemetry_config);

  ResourceProviderImpl resource_provider;
  Resource resource = resource_provider.getResource(opentelemetry_config, context_);

  EXPECT_EQ(resource.schemaUrl, "");

  // service.name receives the unknown value when not configured
  EXPECT_EQ(1, resource.attributes.size());
  auto service_name = resource.attributes.find("service.name");
  EXPECT_EQ("unknown_service:envoy", service_name->second);
}

TEST_F(ResourceProviderTest, MultipleResourceDetectorsConfigured) {
  auto detector_untyped = std::make_shared<NiceMock<SampleDetector>>();
  EXPECT_CALL(*detector_untyped, detect()).WillOnce(Return(resource_untyped_));

  auto detector_typed = std::make_shared<NiceMock<SampleDetector>>();
  EXPECT_CALL(*detector_typed, detect()).WillOnce(Return(resource_typed_));

  UntypedDetectorFactory untyped_factory;
  Registry::InjectFactory<ResourceDetectorFactory> untyped_factory_registration(untyped_factory);

  TypedDetectorFactory typed_factory;
  Registry::InjectFactory<ResourceDetectorTypedFactory> typed_factory_registration(typed_factory);

  EXPECT_CALL(untyped_factory, createResourceDetector(_)).WillOnce(Return(detector_untyped));
  EXPECT_CALL(typed_factory, createTypedResourceDetector(_, _)).WillOnce(Return(detector_typed));

  // Expected merged attributes from all detectors
  ResourceAttributes expected_attributes = {
      {"service.name", "my-service"}, {"key1", "val1"}, {"key2", "val2"}};

  const std::string yaml_string = R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: fake-cluster
      timeout: 0.250s
    service_name: my-service
    resource_detectors:
      - name: envoy.tracers.opentelemetry.resource_detectors.untyped
      - name: envoy.tracers.opentelemetry.resource_detectors.typed
        typed_config:
            "@type": type.googleapis.com/google.protobuf.Struct
    )EOF";
  envoy::config::trace::v3::OpenTelemetryConfig opentelemetry_config;
  TestUtility::loadFromYaml(yaml_string, opentelemetry_config);

  ResourceProviderImpl resource_provider;
  Resource resource = resource_provider.getResource(opentelemetry_config, context_);

  EXPECT_EQ(resource.schemaUrl, "");

  // The resource should contain all 3 merged attributes
  // service.name + 1 for each detector
  EXPECT_EQ(3, resource.attributes.size());

  for (auto& actual : resource.attributes) {
    auto expected = expected_attributes.find(actual.first);

    EXPECT_TRUE(expected != expected_attributes.end());
    EXPECT_EQ(expected->second, actual.second);
  }
}

TEST_F(ResourceProviderTest, UnknownResourceDetectors) {
  // untyped resource detector
  {
    const std::string yaml_string = R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: fake-cluster
      timeout: 0.250s
    service_name: my-service
    resource_detectors:
      - name: envoy.tracers.opentelemetry.resource_detectors.UnkownResourceDetector
    )EOF";
    envoy::config::trace::v3::OpenTelemetryConfig opentelemetry_config;
    TestUtility::loadFromYaml(yaml_string, opentelemetry_config);

    ResourceProviderImpl resource_provider;
    EXPECT_THROW_WITH_MESSAGE(
        resource_provider.getResource(opentelemetry_config, context_), EnvoyException,
        "Resource detector factory not found: "
        "'envoy.tracers.opentelemetry.resource_detectors.UnkownResourceDetector'");
  }
  // typed resource detector
  {
    const std::string yaml_string = R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: fake-cluster
      timeout: 0.250s
    service_name: my-service
    resource_detectors:
      - name: envoy.tracers.opentelemetry.resource_detectors.AnotherUnkownResourceDetector
        typed_config:
            "@type": type.googleapis.com/google.protobuf.Struct
    )EOF";
    envoy::config::trace::v3::OpenTelemetryConfig opentelemetry_config;
    TestUtility::loadFromYaml(yaml_string, opentelemetry_config);

    ResourceProviderImpl resource_provider;
    EXPECT_THROW_WITH_MESSAGE(
        resource_provider.getResource(opentelemetry_config, context_), EnvoyException,
        "Resource detector factory not found: "
        "'envoy.tracers.opentelemetry.resource_detectors.AnotherUnkownResourceDetector'");
  }
}

TEST_F(ResourceProviderTest, ProblemCreatingResourceDetector) {
  UntypedDetectorFactory factory;
  Registry::InjectFactory<ResourceDetectorFactory> factory_registration(factory);

  // Simulating having a problem when creating the resource detector
  EXPECT_CALL(factory, createResourceDetector(_)).WillOnce(Return(nullptr));

  const std::string yaml_string = R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: fake-cluster
      timeout: 0.250s
    service_name: my-service
    resource_detectors:
      - name: envoy.tracers.opentelemetry.resource_detectors.untyped
    )EOF";

  envoy::config::trace::v3::OpenTelemetryConfig opentelemetry_config;
  TestUtility::loadFromYaml(yaml_string, opentelemetry_config);

  ResourceProviderImpl resource_provider;
  EXPECT_THROW_WITH_MESSAGE(resource_provider.getResource(opentelemetry_config, context_),
                            EnvoyException,
                            "Resource detector could not be created: "
                            "'envoy.tracers.opentelemetry.resource_detectors.untyped'");
}

TEST_F(ResourceProviderTest, SchemaUrl) {
  // old resource schema is empty but updating is not. should keep updating schema
  {
    std::string expected_schema_url = "my.schema/v1";
    Resource old_resource = resource_untyped_;

    Resource updating_resource = resource_typed_;
    updating_resource.schemaUrl = expected_schema_url;

    auto detector_untyped = std::make_shared<NiceMock<SampleDetector>>();
    EXPECT_CALL(*detector_untyped, detect()).WillOnce(Return(old_resource));

    auto detector_typed = std::make_shared<NiceMock<SampleDetector>>();
    EXPECT_CALL(*detector_typed, detect()).WillOnce(Return(updating_resource));

    UntypedDetectorFactory untyped_factory;
    Registry::InjectFactory<ResourceDetectorFactory> untyped_factory_registration(untyped_factory);

    TypedDetectorFactory typed_factory;
    Registry::InjectFactory<ResourceDetectorTypedFactory> typed_factory_registration(typed_factory);

    EXPECT_CALL(untyped_factory, createResourceDetector(_)).WillOnce(Return(detector_untyped));
    EXPECT_CALL(typed_factory, createTypedResourceDetector(_, _)).WillOnce(Return(detector_typed));

    const std::string yaml_string = R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: fake-cluster
      timeout: 0.250s
    service_name: my-service
    resource_detectors:
      - name: envoy.tracers.opentelemetry.resource_detectors.untyped
      - name: envoy.tracers.opentelemetry.resource_detectors.typed
        typed_config:
            "@type": type.googleapis.com/google.protobuf.Struct
    )EOF";
    envoy::config::trace::v3::OpenTelemetryConfig opentelemetry_config;
    TestUtility::loadFromYaml(yaml_string, opentelemetry_config);

    ResourceProviderImpl resource_provider;
    Resource resource = resource_provider.getResource(opentelemetry_config, context_);

    EXPECT_EQ(expected_schema_url, resource.schemaUrl);
  }
  // old resource schema is not empty and updating one is. should keep old schema
  {
    std::string expected_schema_url = "my.schema/v1";
    Resource old_resource = resource_untyped_;
    old_resource.schemaUrl = expected_schema_url;

    Resource updating_resource = resource_typed_;
    updating_resource.schemaUrl = "";

    auto detector_untyped = std::make_shared<NiceMock<SampleDetector>>();
    EXPECT_CALL(*detector_untyped, detect()).WillOnce(Return(old_resource));

    auto detector_typed = std::make_shared<NiceMock<SampleDetector>>();
    EXPECT_CALL(*detector_typed, detect()).WillOnce(Return(updating_resource));

    UntypedDetectorFactory untyped_factory;
    Registry::InjectFactory<ResourceDetectorFactory> untyped_factory_registration(untyped_factory);

    TypedDetectorFactory typed_factory;
    Registry::InjectFactory<ResourceDetectorTypedFactory> typed_factory_registration(typed_factory);

    EXPECT_CALL(untyped_factory, createResourceDetector(_)).WillOnce(Return(detector_untyped));
    EXPECT_CALL(typed_factory, createTypedResourceDetector(_, _)).WillOnce(Return(detector_typed));

    const std::string yaml_string = R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: fake-cluster
      timeout: 0.250s
    service_name: my-service
    resource_detectors:
      - name: envoy.tracers.opentelemetry.resource_detectors.untyped
      - name: envoy.tracers.opentelemetry.resource_detectors.typed
        typed_config:
            "@type": type.googleapis.com/google.protobuf.Struct
    )EOF";
    envoy::config::trace::v3::OpenTelemetryConfig opentelemetry_config;
    TestUtility::loadFromYaml(yaml_string, opentelemetry_config);

    ResourceProviderImpl resource_provider;
    Resource resource = resource_provider.getResource(opentelemetry_config, context_);

    EXPECT_EQ(expected_schema_url, resource.schemaUrl);
  }
  // old and updating resource schema are the same. should keep old schema
  {
    std::string expected_schema_url = "my.schema/v1";
    Resource old_resource = resource_untyped_;
    old_resource.schemaUrl = expected_schema_url;

    Resource updating_resource = resource_typed_;
    updating_resource.schemaUrl = expected_schema_url;

    auto detector_untyped = std::make_shared<NiceMock<SampleDetector>>();
    EXPECT_CALL(*detector_untyped, detect()).WillOnce(Return(old_resource));

    auto detector_typed = std::make_shared<NiceMock<SampleDetector>>();
    EXPECT_CALL(*detector_typed, detect()).WillOnce(Return(updating_resource));

    UntypedDetectorFactory untyped_factory;
    Registry::InjectFactory<ResourceDetectorFactory> untyped_factory_registration(untyped_factory);

    TypedDetectorFactory typed_factory;
    Registry::InjectFactory<ResourceDetectorTypedFactory> typed_factory_registration(typed_factory);

    EXPECT_CALL(untyped_factory, createResourceDetector(_)).WillOnce(Return(detector_untyped));
    EXPECT_CALL(typed_factory, createTypedResourceDetector(_, _)).WillOnce(Return(detector_typed));

    const std::string yaml_string = R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: fake-cluster
      timeout: 0.250s
    service_name: my-service
    resource_detectors:
      - name: envoy.tracers.opentelemetry.resource_detectors.untyped
      - name: envoy.tracers.opentelemetry.resource_detectors.typed
        typed_config:
            "@type": type.googleapis.com/google.protobuf.Struct
    )EOF";
    envoy::config::trace::v3::OpenTelemetryConfig opentelemetry_config;
    TestUtility::loadFromYaml(yaml_string, opentelemetry_config);

    ResourceProviderImpl resource_provider;
    Resource resource = resource_provider.getResource(opentelemetry_config, context_);

    EXPECT_EQ(expected_schema_url, resource.schemaUrl);
  }
  // old and updating resource schema are not empty and are different. should keep old schema
  {
    std::string expected_schema_url = "my.schema/v1";
    Resource old_resource = resource_untyped_;
    old_resource.schemaUrl = expected_schema_url;

    Resource updating_resource = resource_typed_;
    updating_resource.schemaUrl = "my.schema/v2";

    auto detector_untyped = std::make_shared<NiceMock<SampleDetector>>();
    EXPECT_CALL(*detector_untyped, detect()).WillOnce(Return(old_resource));

    auto detector_typed = std::make_shared<NiceMock<SampleDetector>>();
    EXPECT_CALL(*detector_typed, detect()).WillOnce(Return(updating_resource));

    UntypedDetectorFactory untyped_factory;
    Registry::InjectFactory<ResourceDetectorFactory> untyped_factory_registration(untyped_factory);

    TypedDetectorFactory typed_factory;
    Registry::InjectFactory<ResourceDetectorTypedFactory> typed_factory_registration(typed_factory);

    EXPECT_CALL(untyped_factory, createResourceDetector(_)).WillOnce(Return(detector_untyped));
    EXPECT_CALL(typed_factory, createTypedResourceDetector(_, _)).WillOnce(Return(detector_typed));

    const std::string yaml_string = R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: fake-cluster
      timeout: 0.250s
    service_name: my-service
    resource_detectors:
      - name: envoy.tracers.opentelemetry.resource_detectors.untyped
      - name: envoy.tracers.opentelemetry.resource_detectors.typed
        typed_config:
            "@type": type.googleapis.com/google.protobuf.Struct
    )EOF";
    envoy::config::trace::v3::OpenTelemetryConfig opentelemetry_config;
    TestUtility::loadFromYaml(yaml_string, opentelemetry_config);

    ResourceProviderImpl resource_provider;
    Resource resource = resource_provider.getResource(opentelemetry_config, context_);

    EXPECT_EQ(expected_schema_url, resource.schemaUrl);
  }
}

} // namespace
} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
