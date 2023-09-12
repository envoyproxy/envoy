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

class DetectorFactoryA : public ResourceDetectorFactory {
public:
  MOCK_METHOD(ResourceDetectorPtr, createResourceDetector,
              (const Protobuf::Message& message,
               Server::Configuration::TracerFactoryContext& context));

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::Struct>();
  }

  std::string name() const override { return "envoy.tracers.opentelemetry.resource_detectors.a"; }
};

class DetectorFactoryB : public ResourceDetectorFactory {
public:
  MOCK_METHOD(ResourceDetectorPtr, createResourceDetector,
              (const Protobuf::Message& message,
               Server::Configuration::TracerFactoryContext& context));

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::StringValue>();
  }

  std::string name() const override { return "envoy.tracers.opentelemetry.resource_detectors.b"; }
};

const std::string kOtelResourceAttributesEnv = "OTEL_RESOURCE_ATTRIBUTES";

class ResourceProviderTest : public testing::Test {
public:
  ResourceProviderTest() {
    resource_a_.attributes.insert(std::pair<std::string, std::string>("key1", "val1"));
    resource_b_.attributes.insert(std::pair<std::string, std::string>("key2", "val2"));
  }
  NiceMock<Server::Configuration::MockTracerFactoryContext> context_;
  Resource resource_a_;
  Resource resource_b_;
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
  auto detector_a = std::make_shared<NiceMock<SampleDetector>>();
  EXPECT_CALL(*detector_a, detect()).WillOnce(Return(resource_a_));

  auto detector_b = std::make_shared<NiceMock<SampleDetector>>();
  EXPECT_CALL(*detector_b, detect()).WillOnce(Return(resource_b_));

  DetectorFactoryA factory_a;
  Registry::InjectFactory<ResourceDetectorFactory> factory_a_registration(factory_a);

  DetectorFactoryB factory_b;
  Registry::InjectFactory<ResourceDetectorFactory> factory_b_registration(factory_b);

  EXPECT_CALL(factory_a, createResourceDetector(_, _)).WillOnce(Return(detector_a));
  EXPECT_CALL(factory_b, createResourceDetector(_, _)).WillOnce(Return(detector_b));

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
      - name: envoy.tracers.opentelemetry.resource_detectors.a
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
      - name: envoy.tracers.opentelemetry.resource_detectors.b
        typed_config:
          "@type": type.googleapis.com/google.protobuf.StringValue
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
  const std::string yaml_string = R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: fake-cluster
      timeout: 0.250s
    service_name: my-service
    resource_detectors:
      - name: envoy.tracers.opentelemetry.resource_detectors.UnkownResourceDetector
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
    )EOF";
  envoy::config::trace::v3::OpenTelemetryConfig opentelemetry_config;
  TestUtility::loadFromYaml(yaml_string, opentelemetry_config);

  ResourceProviderImpl resource_provider;
  EXPECT_THROW_WITH_MESSAGE(
      resource_provider.getResource(opentelemetry_config, context_), EnvoyException,
      "Resource detector factory not found: "
      "'envoy.tracers.opentelemetry.resource_detectors.UnkownResourceDetector'");
}

TEST_F(ResourceProviderTest, ProblemCreatingResourceDetector) {
  DetectorFactoryA factory;
  Registry::InjectFactory<ResourceDetectorFactory> factory_registration(factory);

  // Simulating having a problem when creating the resource detector
  EXPECT_CALL(factory, createResourceDetector(_, _)).WillOnce(Return(nullptr));

  const std::string yaml_string = R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: fake-cluster
      timeout: 0.250s
    service_name: my-service
    resource_detectors:
      - name: envoy.tracers.opentelemetry.resource_detectors.a
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
    )EOF";

  envoy::config::trace::v3::OpenTelemetryConfig opentelemetry_config;
  TestUtility::loadFromYaml(yaml_string, opentelemetry_config);

  ResourceProviderImpl resource_provider;
  EXPECT_THROW_WITH_MESSAGE(resource_provider.getResource(opentelemetry_config, context_),
                            EnvoyException,
                            "Resource detector could not be created: "
                            "'envoy.tracers.opentelemetry.resource_detectors.a'");
}

TEST_F(ResourceProviderTest, SchemaUrl) {
  // old resource schema is empty but updating is not. should keep updating schema
  {
    std::string expected_schema_url = "my.schema/v1";
    Resource old_resource = resource_a_;

    Resource updating_resource = resource_b_;
    updating_resource.schemaUrl = expected_schema_url;

    auto detector_a = std::make_shared<NiceMock<SampleDetector>>();
    EXPECT_CALL(*detector_a, detect()).WillOnce(Return(old_resource));

    auto detector_b = std::make_shared<NiceMock<SampleDetector>>();
    EXPECT_CALL(*detector_b, detect()).WillOnce(Return(updating_resource));

    DetectorFactoryA factory_a;
    Registry::InjectFactory<ResourceDetectorFactory> factory_a_registration(factory_a);

    DetectorFactoryB factory_b;
    Registry::InjectFactory<ResourceDetectorFactory> factory_b_registration(factory_b);

    EXPECT_CALL(factory_a, createResourceDetector(_, _)).WillOnce(Return(detector_a));
    EXPECT_CALL(factory_b, createResourceDetector(_, _)).WillOnce(Return(detector_b));

    const std::string yaml_string = R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: fake-cluster
      timeout: 0.250s
    service_name: my-service
    resource_detectors:
      - name: envoy.tracers.opentelemetry.resource_detectors.a
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
      - name: envoy.tracers.opentelemetry.resource_detectors.b
        typed_config:
          "@type": type.googleapis.com/google.protobuf.StringValue
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
    Resource old_resource = resource_a_;
    old_resource.schemaUrl = expected_schema_url;

    Resource updating_resource = resource_b_;
    updating_resource.schemaUrl = "";

    auto detector_a = std::make_shared<NiceMock<SampleDetector>>();
    EXPECT_CALL(*detector_a, detect()).WillOnce(Return(old_resource));

    auto detector_b = std::make_shared<NiceMock<SampleDetector>>();
    EXPECT_CALL(*detector_b, detect()).WillOnce(Return(updating_resource));

    DetectorFactoryA factory_a;
    Registry::InjectFactory<ResourceDetectorFactory> factory_a_registration(factory_a);

    DetectorFactoryB factory_b;
    Registry::InjectFactory<ResourceDetectorFactory> factory_b_registration(factory_b);

    EXPECT_CALL(factory_a, createResourceDetector(_, _)).WillOnce(Return(detector_a));
    EXPECT_CALL(factory_b, createResourceDetector(_, _)).WillOnce(Return(detector_b));

    const std::string yaml_string = R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: fake-cluster
      timeout: 0.250s
    service_name: my-service
    resource_detectors:
      - name: envoy.tracers.opentelemetry.resource_detectors.a
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
      - name: envoy.tracers.opentelemetry.resource_detectors.b
        typed_config:
          "@type": type.googleapis.com/google.protobuf.StringValue
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
    Resource old_resource = resource_a_;
    old_resource.schemaUrl = expected_schema_url;

    Resource updating_resource = resource_b_;
    updating_resource.schemaUrl = expected_schema_url;

    auto detector_a = std::make_shared<NiceMock<SampleDetector>>();
    EXPECT_CALL(*detector_a, detect()).WillOnce(Return(old_resource));

    auto detector_b = std::make_shared<NiceMock<SampleDetector>>();
    EXPECT_CALL(*detector_b, detect()).WillOnce(Return(updating_resource));

    DetectorFactoryA factory_a;
    Registry::InjectFactory<ResourceDetectorFactory> factory_a_registration(factory_a);

    DetectorFactoryB factory_b;
    Registry::InjectFactory<ResourceDetectorFactory> factory_b_registration(factory_b);

    EXPECT_CALL(factory_a, createResourceDetector(_, _)).WillOnce(Return(detector_a));
    EXPECT_CALL(factory_b, createResourceDetector(_, _)).WillOnce(Return(detector_b));

    const std::string yaml_string = R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: fake-cluster
      timeout: 0.250s
    service_name: my-service
    resource_detectors:
      - name: envoy.tracers.opentelemetry.resource_detectors.a
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
      - name: envoy.tracers.opentelemetry.resource_detectors.b
        typed_config:
          "@type": type.googleapis.com/google.protobuf.StringValue
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
    Resource old_resource = resource_a_;
    old_resource.schemaUrl = expected_schema_url;

    Resource updating_resource = resource_b_;
    updating_resource.schemaUrl = "my.schema/v2";

    auto detector_a = std::make_shared<NiceMock<SampleDetector>>();
    EXPECT_CALL(*detector_a, detect()).WillOnce(Return(old_resource));

    auto detector_b = std::make_shared<NiceMock<SampleDetector>>();
    EXPECT_CALL(*detector_b, detect()).WillOnce(Return(updating_resource));

    DetectorFactoryA factory_a;
    Registry::InjectFactory<ResourceDetectorFactory> factory_a_registration(factory_a);

    DetectorFactoryB factory_b;
    Registry::InjectFactory<ResourceDetectorFactory> factory_b_registration(factory_b);

    EXPECT_CALL(factory_a, createResourceDetector(_, _)).WillOnce(Return(detector_a));
    EXPECT_CALL(factory_b, createResourceDetector(_, _)).WillOnce(Return(detector_b));

    const std::string yaml_string = R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: fake-cluster
      timeout: 0.250s
    service_name: my-service
    resource_detectors:
      - name: envoy.tracers.opentelemetry.resource_detectors.a
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
      - name: envoy.tracers.opentelemetry.resource_detectors.b
        typed_config:
          "@type": type.googleapis.com/google.protobuf.StringValue
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
