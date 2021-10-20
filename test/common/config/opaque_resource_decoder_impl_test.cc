#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.validate.h"

#include "source/common/config/opaque_resource_decoder_impl.h"
#include "source/common/protobuf/message_validator_impl.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

class OpaqueResourceDecoderImplTest : public testing::Test {
public:
  std::pair<ProtobufTypes::MessagePtr, std::string>
  decodeTypedResource(const envoy::config::endpoint::v3::ClusterLoadAssignment& typed_resource) {
    ProtobufWkt::Any opaque_resource;
    opaque_resource.PackFrom(typed_resource);
    auto decoded_resource = resource_decoder_.decodeResource(opaque_resource);
    const std::string name = resource_decoder_.resourceName(*decoded_resource);
    return {std::move(decoded_resource), name};
  }

  ProtobufMessage::StrictValidationVisitorImpl validation_visitor_;
  OpaqueResourceDecoderImpl<envoy::config::endpoint::v3::ClusterLoadAssignment> resource_decoder_{
      validation_visitor_, "cluster_name"};
};

// Negative test for bad type URL in Any.
TEST_F(OpaqueResourceDecoderImplTest, WrongType) {
  ProtobufWkt::Any opaque_resource;
  opaque_resource.set_type_url("huh");
  EXPECT_THROW_WITH_REGEX(resource_decoder_.decodeResource(opaque_resource), EnvoyException,
                          "Unable to unpack");
}

// If the Any is empty (no type set), the default instance of the opaque resource decoder type is
// created.
TEST_F(OpaqueResourceDecoderImplTest, Empty) {
  ProtobufWkt::Any opaque_resource;
  const auto decoded_resource = resource_decoder_.decodeResource(opaque_resource);
  EXPECT_THAT(*decoded_resource, ProtoEq(envoy::config::endpoint::v3::ClusterLoadAssignment()));
  EXPECT_EQ("", resource_decoder_.resourceName(*decoded_resource));
}

// Negative test for protoc-gen-validate constraints.
TEST_F(OpaqueResourceDecoderImplTest, ValidateFail) {
  envoy::config::endpoint::v3::ClusterLoadAssignment invalid_resource;
  EXPECT_THROW(decodeTypedResource(invalid_resource), ProtoValidationException);
}

// When validation is skipped, verify that we can ignore unknown fields.
TEST_F(OpaqueResourceDecoderImplTest, ValidateIgnored) {
  ProtobufMessage::NullValidationVisitorImpl validation_visitor;
  OpaqueResourceDecoderImpl<envoy::config::endpoint::v3::ClusterLoadAssignment> resource_decoder{
      validation_visitor, "cluster_name"};
  envoy::config::endpoint::v3::ClusterLoadAssignment strange_resource;
  strange_resource.set_cluster_name("fare");
  auto* unknown = strange_resource.GetReflection()->MutableUnknownFields(&strange_resource);
  // add a field that doesn't exist in the proto definition:
  unknown->AddFixed32(1000, 1);
  ProtobufWkt::Any opaque_resource;
  opaque_resource.PackFrom(strange_resource);
  const auto decoded_resource = resource_decoder.decodeResource(opaque_resource);
  EXPECT_THAT(*decoded_resource, ProtoEq(strange_resource));
  EXPECT_EQ("fare", resource_decoder_.resourceName(*decoded_resource));
}

// Happy path.
TEST_F(OpaqueResourceDecoderImplTest, Success) {
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_resource;
  cluster_resource.set_cluster_name("foo");
  const auto result = decodeTypedResource(cluster_resource);
  EXPECT_THAT(*result.first, ProtoEq(cluster_resource));
  EXPECT_EQ("foo", result.second);
}

} // namespace
} // namespace Config
} // namespace Envoy
