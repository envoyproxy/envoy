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
    resource_a_.attributes_.insert(std::pair<std::string, std::string>("key1", "val1"));
    resource_b_.attributes_.insert(std::pair<std::string, std::string>("key2", "val2"));
  }
  NiceMock<Server::Configuration::MockTracerFactoryContext> context_;
  Resource resource_a_;
  Resource resource_b_;
};

// Verifies a resource with the static service name is returned when no detectors are configured
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

  EXPECT_EQ(resource.schema_url_, "");

  // Only the service name was added to the resource
  EXPECT_EQ(1, resource.attributes_.size());
}

// Verifies a resource with the default service name is returned when no detectors + static service
// name are configured
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

  EXPECT_EQ(resource.schema_url_, "");

  // service.name receives the unknown value when not configured
  EXPECT_EQ(1, resource.attributes_.size());
  auto service_name = resource.attributes_.find("service.name");
  EXPECT_EQ("unknown_service:envoy", service_name->second);
}

// Verifies it is possible to configure multiple resource detectors
TEST_F(ResourceProviderTest, MultipleResourceDetectorsConfigured) {
  auto detector_a = std::make_unique<NiceMock<SampleDetector>>();
  EXPECT_CALL(*detector_a, detect()).WillOnce(Return(resource_a_));

  auto detector_b = std::make_unique<NiceMock<SampleDetector>>();
  EXPECT_CALL(*detector_b, detect()).WillOnce(Return(resource_b_));

  DetectorFactoryA factory_a;
  Registry::InjectFactory<ResourceDetectorFactory> factory_a_registration(factory_a);

  DetectorFactoryB factory_b;
  Registry::InjectFactory<ResourceDetectorFactory> factory_b_registration(factory_b);

  EXPECT_CALL(factory_a, createResourceDetector(_, _))
      .WillOnce(Return(testing::ByMove(std::move(detector_a))));
  EXPECT_CALL(factory_b, createResourceDetector(_, _))
      .WillOnce(Return(testing::ByMove(std::move(detector_b))));

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

  EXPECT_EQ(resource.schema_url_, "");

  // The resource should contain all 3 merged attributes
  // service.name + 1 for each detector
  EXPECT_EQ(3, resource.attributes_.size());

  for (auto& actual : resource.attributes_) {
    auto expected = expected_attributes.find(actual.first);

    EXPECT_TRUE(expected != expected_attributes.end());
    EXPECT_EQ(expected->second, actual.second);
  }
}

// Verifies Envoy fails when an unknown resource detector is configured
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

// Verifies Envoy fails when an error occurs while instantiating a resource detector
TEST_F(ResourceProviderTest, ProblemCreatingResourceDetector) {
  DetectorFactoryA factory;
  Registry::InjectFactory<ResourceDetectorFactory> factory_registration(factory);

  // Simulating having a problem when creating the resource detector
  EXPECT_CALL(factory, createResourceDetector(_, _)).WillOnce(Return(testing::ByMove(nullptr)));

  const std::string yaml_string = R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: fake-clusterdetector_a
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

// Test merge when old schema url is empty but updating is not
TEST_F(ResourceProviderTest, OldSchemaEmptyUpdatingSet) {
  std::string expected_schema_url = "my.schema/v1";
  Resource old_resource = resource_a_;

  // Updating resource is empty (no attributes)
  Resource updating_resource;
  updating_resource.schema_url_ = expected_schema_url;

  auto detector_a = std::make_unique<NiceMock<SampleDetector>>();
  EXPECT_CALL(*detector_a, detect()).WillOnce(Return(old_resource));

  auto detector_b = std::make_unique<NiceMock<SampleDetector>>();
  EXPECT_CALL(*detector_b, detect()).WillOnce(Return(updating_resource));

  DetectorFactoryA factory_a;
  Registry::InjectFactory<ResourceDetectorFactory> factory_a_registration(factory_a);

  DetectorFactoryB factory_b;
  Registry::InjectFactory<ResourceDetectorFactory> factory_b_registration(factory_b);

  EXPECT_CALL(factory_a, createResourceDetector(_, _))
      .WillOnce(Return(testing::ByMove(std::move(detector_a))));
  EXPECT_CALL(factory_b, createResourceDetector(_, _))
      .WillOnce(Return(testing::ByMove(std::move(detector_b))));

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

  // OTel spec says the updating schema should be used
  EXPECT_EQ(expected_schema_url, resource.schema_url_);
}

// Test merge when old schema url is not empty but updating is
TEST_F(ResourceProviderTest, OldSchemaSetUpdatingEmpty) {
  std::string expected_schema_url = "my.schema/v1";
  Resource old_resource = resource_a_;
  old_resource.schema_url_ = expected_schema_url;

  Resource updating_resource = resource_b_;
  updating_resource.schema_url_ = "";

  auto detector_a = std::make_unique<NiceMock<SampleDetector>>();
  EXPECT_CALL(*detector_a, detect()).WillOnce(Return(old_resource));

  auto detector_b = std::make_unique<NiceMock<SampleDetector>>();
  EXPECT_CALL(*detector_b, detect()).WillOnce(Return(updating_resource));

  DetectorFactoryA factory_a;
  Registry::InjectFactory<ResourceDetectorFactory> factory_a_registration(factory_a);

  DetectorFactoryB factory_b;
  Registry::InjectFactory<ResourceDetectorFactory> factory_b_registration(factory_b);

  EXPECT_CALL(factory_a, createResourceDetector(_, _))
      .WillOnce(Return(testing::ByMove(std::move(detector_a))));
  EXPECT_CALL(factory_b, createResourceDetector(_, _))
      .WillOnce(Return(testing::ByMove(std::move(detector_b))));

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

  // OTel spec says the updating schema should be used
  EXPECT_EQ(expected_schema_url, resource.schema_url_);
}

// Test merge when both old and updating schema url are set and equal
TEST_F(ResourceProviderTest, OldAndUpdatingSchemaAreEqual) {
  std::string expected_schema_url = "my.schema/v1";
  Resource old_resource = resource_a_;
  old_resource.schema_url_ = expected_schema_url;

  Resource updating_resource = resource_b_;
  updating_resource.schema_url_ = expected_schema_url;

  auto detector_a = std::make_unique<NiceMock<SampleDetector>>();
  EXPECT_CALL(*detector_a, detect()).WillOnce(Return(old_resource));

  auto detector_b = std::make_unique<NiceMock<SampleDetector>>();
  EXPECT_CALL(*detector_b, detect()).WillOnce(Return(updating_resource));

  DetectorFactoryA factory_a;
  Registry::InjectFactory<ResourceDetectorFactory> factory_a_registration(factory_a);

  DetectorFactoryB factory_b;
  Registry::InjectFactory<ResourceDetectorFactory> factory_b_registration(factory_b);

  EXPECT_CALL(factory_a, createResourceDetector(_, _))
      .WillOnce(Return(testing::ByMove(std::move(detector_a))));
  EXPECT_CALL(factory_b, createResourceDetector(_, _))
      .WillOnce(Return(testing::ByMove(std::move(detector_b))));

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

  EXPECT_EQ(expected_schema_url, resource.schema_url_);
}

// Test merge when both old and updating schema url are set but different
TEST_F(ResourceProviderTest, OldAndUpdatingSchemaAreDifferent) {
  std::string expected_schema_url = "my.schema/v1";
  Resource old_resource = resource_a_;
  old_resource.schema_url_ = expected_schema_url;

  Resource updating_resource = resource_b_;
  updating_resource.schema_url_ = "my.schema/v2";

  auto detector_a = std::make_unique<NiceMock<SampleDetector>>();
  EXPECT_CALL(*detector_a, detect()).WillOnce(Return(old_resource));

  auto detector_b = std::make_unique<NiceMock<SampleDetector>>();
  EXPECT_CALL(*detector_b, detect()).WillOnce(Return(updating_resource));

  DetectorFactoryA factory_a;
  Registry::InjectFactory<ResourceDetectorFactory> factory_a_registration(factory_a);

  DetectorFactoryB factory_b;
  Registry::InjectFactory<ResourceDetectorFactory> factory_b_registration(factory_b);

  EXPECT_CALL(factory_a, createResourceDetector(_, _))
      .WillOnce(Return(testing::ByMove(std::move(detector_a))));
  EXPECT_CALL(factory_b, createResourceDetector(_, _))
      .WillOnce(Return(testing::ByMove(std::move(detector_b))));

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

  // OTel spec says Old schema should be used
  EXPECT_EQ(expected_schema_url, resource.schema_url_);
}

} // namespace
} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
