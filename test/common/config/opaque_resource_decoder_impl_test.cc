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
  std::pair<Protobuf::Arena::UniquePtr<Protobuf::Message>, std::string>
  decodeTypedResource(const envoy::config::endpoint::v3::ClusterLoadAssignment& typed_resource) {
    Protobuf::Any opaque_resource;
    std::ignore = opaque_resource.PackFrom(typed_resource);
    auto decoded_resource = resource_decoder_.decodeResource(opaque_resource, arena_);
    const std::string name = resource_decoder_.resourceName(*decoded_resource);
    return {std::move(decoded_resource), name};
  }

  ProtobufMessage::StrictValidationVisitorImpl validation_visitor_;
  OpaqueResourceDecoderImpl<envoy::config::endpoint::v3::ClusterLoadAssignment> resource_decoder_{
      validation_visitor_, "cluster_name"};
  Protobuf::Arena arena_{resource_decoder_.arenaOptions()};
};

// Negative test for bad type URL in Any.
TEST_F(OpaqueResourceDecoderImplTest, WrongType) {
  Protobuf::Any opaque_resource;
  opaque_resource.set_type_url("huh");
  EXPECT_THROW_WITH_REGEX(std::ignore = resource_decoder_.decodeResource(opaque_resource, arena_),
                          EnvoyException, "Unable to unpack");
}

// If the Any is empty (no type set), the default instance of the opaque resource decoder type is
// created.
TEST_F(OpaqueResourceDecoderImplTest, Empty) {
  Protobuf::Any opaque_resource;
  const auto decoded_resource = resource_decoder_.decodeResource(opaque_resource, arena_);
  EXPECT_THAT(*decoded_resource, ProtoEq(envoy::config::endpoint::v3::ClusterLoadAssignment()));
  EXPECT_EQ("", resource_decoder_.resourceName(*decoded_resource));
  EXPECT_EQ(&arena_, decoded_resource->GetArena());
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
  Protobuf::Any opaque_resource;
  std::ignore = opaque_resource.PackFrom(strange_resource);
  const auto decoded_resource = resource_decoder.decodeResource(opaque_resource, arena_);
  EXPECT_THAT(*decoded_resource, ProtoEq(strange_resource));
  EXPECT_EQ("fare", resource_decoder_.resourceName(*decoded_resource));
  EXPECT_EQ(&arena_, decoded_resource->GetArena());
}

// Happy path.
TEST_F(OpaqueResourceDecoderImplTest, Success) {
  EXPECT_EQ(8 * 1024, resource_decoder_.arenaOptions().start_block_size);
  EXPECT_EQ(1024 * 1024, resource_decoder_.arenaOptions().max_block_size);
  envoy::config::endpoint::v3::ClusterLoadAssignment cluster_resource;
  cluster_resource.set_cluster_name("foo");
  const auto result = decodeTypedResource(cluster_resource);
  EXPECT_THAT(*result.first, ProtoEq(cluster_resource));
  EXPECT_EQ("foo", result.second);
  EXPECT_EQ(&arena_, result.first->GetArena());
}

} // namespace
} // namespace Config
} // namespace Envoy
