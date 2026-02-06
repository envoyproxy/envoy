#include "source/common/config/decoded_resource_impl.h"

#include "test/mocks/config/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using ::testing::InvokeWithoutArgs;
using ::testing::Return;

namespace Envoy {
namespace Config {
namespace {

TEST(DecodedResourceImplTest, All) {
  MockOpaqueResourceDecoder resource_decoder;
  Protobuf::Any some_opaque_resource;
  some_opaque_resource.set_type_url("some_type_url");

  {
    EXPECT_CALL(resource_decoder, decodeResource(ProtoEq(some_opaque_resource)))
        .WillOnce(InvokeWithoutArgs(
            []() -> ProtobufTypes::MessagePtr { return std::make_unique<Protobuf::Empty>(); }));
    EXPECT_CALL(resource_decoder, resourceName(ProtoEq(Protobuf::Empty())))
        .WillOnce(Return("some_name"));
    auto decoded_resource =
        *DecodedResourceImpl::fromResource(resource_decoder, some_opaque_resource, "foo");
    EXPECT_EQ("some_name", decoded_resource->name());
    EXPECT_TRUE(decoded_resource->aliases().empty());
    EXPECT_EQ("foo", decoded_resource->version());
    EXPECT_THAT(decoded_resource->resource(), ProtoEq(Protobuf::Empty()));
    EXPECT_TRUE(decoded_resource->hasResource());
  }

  {
    envoy::service::discovery::v3::Resource resource_wrapper;
    resource_wrapper.set_name("real_name");
    resource_wrapper.add_aliases("bar");
    resource_wrapper.add_aliases("baz");
    resource_wrapper.mutable_resource()->MergeFrom(some_opaque_resource);
    resource_wrapper.set_version("foo");
    EXPECT_CALL(resource_decoder, decodeResource(ProtoEq(some_opaque_resource)))
        .WillOnce(InvokeWithoutArgs(
            []() -> ProtobufTypes::MessagePtr { return std::make_unique<Protobuf::Empty>(); }));
    EXPECT_CALL(resource_decoder, resourceName(ProtoEq(Protobuf::Empty()))).Times(0);
    DecodedResourceImpl decoded_resource(resource_decoder, resource_wrapper);
    EXPECT_EQ("real_name", decoded_resource.name());
    EXPECT_EQ((std::vector<std::string>{"bar", "baz"}), decoded_resource.aliases());
    EXPECT_EQ("foo", decoded_resource.version());
    EXPECT_THAT(decoded_resource.resource(), ProtoEq(Protobuf::Empty()));
    EXPECT_TRUE(decoded_resource.hasResource());
    EXPECT_FALSE(decoded_resource.metadata().has_value());
    EXPECT_FALSE(decoded_resource.dynamicParameterConstraints().has_value());
  }

  // To verify the metadata is decoded as expected.
  {
    envoy::service::discovery::v3::Resource resource_wrapper;
    resource_wrapper.set_name("real_name");
    resource_wrapper.mutable_resource()->MergeFrom(some_opaque_resource);
    auto metadata = resource_wrapper.mutable_metadata();
    metadata->mutable_filter_metadata()->insert(
        {"fake_test_domain", MessageUtil::keyValueStruct("fake_test_key", "fake_test_value")});
    EXPECT_CALL(resource_decoder, decodeResource(ProtoEq(some_opaque_resource)))
        .WillOnce(InvokeWithoutArgs(
            []() -> ProtobufTypes::MessagePtr { return std::make_unique<Protobuf::Empty>(); }));
    EXPECT_CALL(resource_decoder, resourceName(ProtoEq(Protobuf::Empty()))).Times(0);
    DecodedResourceImpl decoded_resource(resource_decoder, resource_wrapper);
    EXPECT_EQ("real_name", decoded_resource.name());
    EXPECT_THAT(decoded_resource.resource(), ProtoEq(Protobuf::Empty()));
    EXPECT_TRUE(decoded_resource.hasResource());
    EXPECT_TRUE(decoded_resource.metadata().has_value());
    EXPECT_EQ(metadata->DebugString(), decoded_resource.metadata()->DebugString());
  }

  // To verify the metadata is decoded as expected for the fromResource variant
  // with Protobuf::Any& input.
  {
    envoy::service::discovery::v3::Resource resource_wrapper;
    resource_wrapper.set_name("real_name");
    resource_wrapper.mutable_resource()->MergeFrom(some_opaque_resource);
    auto metadata = resource_wrapper.mutable_metadata();
    metadata->mutable_filter_metadata()->insert(
        {"fake_test_domain", MessageUtil::keyValueStruct("fake_test_key", "fake_test_value")});
    Protobuf::Any resource_any;
    resource_any.PackFrom(resource_wrapper);
    EXPECT_CALL(resource_decoder, decodeResource(ProtoEq(some_opaque_resource)))
        .WillOnce(InvokeWithoutArgs(
            []() -> ProtobufTypes::MessagePtr { return std::make_unique<Protobuf::Empty>(); }));
    EXPECT_CALL(resource_decoder, resourceName(ProtoEq(Protobuf::Empty()))).Times(0);
    DecodedResourceImplPtr decoded_resource =
        *DecodedResourceImpl::fromResource(resource_decoder, resource_any, "1");
    EXPECT_EQ("real_name", decoded_resource->name());
    EXPECT_THAT(decoded_resource->resource(), ProtoEq(Protobuf::Empty()));
    EXPECT_TRUE(decoded_resource->hasResource());
    EXPECT_TRUE(decoded_resource->metadata().has_value());
    EXPECT_EQ(metadata->DebugString(), decoded_resource->metadata()->DebugString());
  }

  {
    envoy::service::discovery::v3::Resource resource_wrapper;
    resource_wrapper.set_name("real_name");
    resource_wrapper.set_version("foo");
    resource_wrapper.add_aliases("bar");
    resource_wrapper.add_aliases("baz");
    EXPECT_CALL(resource_decoder, decodeResource(ProtoEq(Protobuf::Any())))
        .WillOnce(InvokeWithoutArgs(
            []() -> ProtobufTypes::MessagePtr { return std::make_unique<Protobuf::Empty>(); }));
    EXPECT_CALL(resource_decoder, resourceName(_)).Times(0);
    DecodedResourceImpl decoded_resource(resource_decoder, resource_wrapper);
    EXPECT_EQ("real_name", decoded_resource.name());
    EXPECT_EQ((std::vector<std::string>{"bar", "baz"}), decoded_resource.aliases());
    EXPECT_EQ("foo", decoded_resource.version());
    EXPECT_THAT(decoded_resource.resource(), ProtoEq(Protobuf::Empty()));
    EXPECT_FALSE(decoded_resource.hasResource());
  }

  {
    envoy::service::discovery::v3::Resource resource_wrapper;
    resource_wrapper.set_name("real_name");
    resource_wrapper.add_aliases("bar");
    resource_wrapper.add_aliases("baz");
    resource_wrapper.mutable_resource()->MergeFrom(some_opaque_resource);
    resource_wrapper.set_version("foo");
    auto metadata = resource_wrapper.mutable_metadata();
    metadata->mutable_filter_metadata()->insert(
        {"fake_test_domain", MessageUtil::keyValueStruct("fake_test_key", "fake_test_value")});
    EXPECT_CALL(resource_decoder, decodeResource(ProtoEq(some_opaque_resource)))
        .WillOnce(InvokeWithoutArgs(
            []() -> ProtobufTypes::MessagePtr { return std::make_unique<Protobuf::Empty>(); }));
    EXPECT_CALL(resource_decoder, resourceName(ProtoEq(Protobuf::Empty()))).Times(0);
    DecodedResourceImplPtr decoded_resource =
        DecodedResourceImpl::fromResource(resource_decoder, resource_wrapper);
    EXPECT_EQ("real_name", decoded_resource->name());
    EXPECT_EQ((std::vector<std::string>{"bar", "baz"}), decoded_resource->aliases());
    EXPECT_EQ("foo", decoded_resource->version());
    EXPECT_THAT(decoded_resource->resource(), ProtoEq(Protobuf::Empty()));
    EXPECT_TRUE(decoded_resource->hasResource());
    EXPECT_TRUE(decoded_resource->metadata().has_value());
    EXPECT_EQ(metadata->DebugString(), decoded_resource->metadata()->DebugString());
  }

  {
    auto message = std::make_unique<Protobuf::Empty>();
    DecodedResourceImpl decoded_resource(std::move(message), "real_name", {"bar", "baz"}, "foo");
    EXPECT_EQ("real_name", decoded_resource.name());
    EXPECT_EQ((std::vector<std::string>{"bar", "baz"}), decoded_resource.aliases());
    EXPECT_EQ("foo", decoded_resource.version());
    EXPECT_THAT(decoded_resource.resource(), ProtoEq(Protobuf::Empty()));
    EXPECT_TRUE(decoded_resource.hasResource());
  }

  // To verify the name is correctly retrieved when only the resource_name field is set.
  {
    envoy::service::discovery::v3::Resource resource_wrapper;
    resource_wrapper.mutable_resource_name()->set_name("resource_name");
    resource_wrapper.add_aliases("bar");
    resource_wrapper.add_aliases("baz");
    resource_wrapper.mutable_resource()->MergeFrom(some_opaque_resource);
    resource_wrapper.set_version("foo");
    EXPECT_CALL(resource_decoder, decodeResource(ProtoEq(some_opaque_resource)))
        .WillOnce(InvokeWithoutArgs(
            []() -> ProtobufTypes::MessagePtr { return std::make_unique<Protobuf::Empty>(); }));
    EXPECT_CALL(resource_decoder, resourceName(ProtoEq(Protobuf::Empty()))).Times(0);
    DecodedResourceImpl decoded_resource(resource_decoder, resource_wrapper);
    EXPECT_EQ("resource_name", decoded_resource.name());
    EXPECT_EQ((std::vector<std::string>{"bar", "baz"}), decoded_resource.aliases());
    EXPECT_EQ("foo", decoded_resource.version());
    EXPECT_THAT(decoded_resource.resource(), ProtoEq(Protobuf::Empty()));
    EXPECT_TRUE(decoded_resource.hasResource());
    EXPECT_FALSE(decoded_resource.metadata().has_value());
  }

  // To verify the resource_name field takes precedence over name when both are set.
  {
    envoy::service::discovery::v3::Resource resource_wrapper;
    resource_wrapper.set_name("real_name");
    resource_wrapper.mutable_resource_name()->set_name("resource_name");
    resource_wrapper.add_aliases("bar");
    resource_wrapper.add_aliases("baz");
    resource_wrapper.mutable_resource()->MergeFrom(some_opaque_resource);
    resource_wrapper.set_version("foo");
    EXPECT_CALL(resource_decoder, decodeResource(ProtoEq(some_opaque_resource)))
        .WillOnce(InvokeWithoutArgs(
            []() -> ProtobufTypes::MessagePtr { return std::make_unique<Protobuf::Empty>(); }));
    EXPECT_CALL(resource_decoder, resourceName(ProtoEq(Protobuf::Empty()))).Times(0);
    DecodedResourceImpl decoded_resource(resource_decoder, resource_wrapper);
    EXPECT_EQ("resource_name", decoded_resource.name());
    EXPECT_EQ((std::vector<std::string>{"bar", "baz"}), decoded_resource.aliases());
    EXPECT_EQ("foo", decoded_resource.version());
    EXPECT_THAT(decoded_resource.resource(), ProtoEq(Protobuf::Empty()));
    EXPECT_TRUE(decoded_resource.hasResource());
    EXPECT_FALSE(decoded_resource.metadata().has_value());
  }

  // To verify the dynamic parameter constraints are decoded as expected.
  // Scenario: env=prod AND NOT version exists
  {
    envoy::service::discovery::v3::Resource resource_wrapper;
    resource_wrapper.mutable_resource_name()->set_name("resource_name");
    auto* constraints =
        resource_wrapper.mutable_resource_name()->mutable_dynamic_parameter_constraints();
    auto* and_constraints = constraints->mutable_and_constraints();
    auto* c1 = and_constraints->add_constraints()->mutable_constraint();
    c1->set_key("env");
    c1->set_value("prod");
    auto* c2 = and_constraints->add_constraints()->mutable_not_constraints();
    c2->mutable_constraint()->set_key("version");
    c2->mutable_constraint()->mutable_exists();
    resource_wrapper.mutable_resource()->MergeFrom(some_opaque_resource);
    resource_wrapper.set_version("foo");
    EXPECT_CALL(resource_decoder, decodeResource(ProtoEq(some_opaque_resource)))
        .WillOnce(InvokeWithoutArgs(
            []() -> ProtobufTypes::MessagePtr { return std::make_unique<Protobuf::Empty>(); }));
    EXPECT_CALL(resource_decoder, resourceName(ProtoEq(Protobuf::Empty()))).Times(0);
    DecodedResourceImpl decoded_resource(resource_decoder, resource_wrapper);
    EXPECT_EQ("resource_name", decoded_resource.name());
    EXPECT_THAT(decoded_resource.resource(), ProtoEq(Protobuf::Empty()));
    EXPECT_TRUE(decoded_resource.hasResource());
    EXPECT_TRUE(decoded_resource.dynamicParameterConstraints().has_value());
    const auto& preserved_constraints =
        decoded_resource.dynamicParameterConstraints().value().get();
    EXPECT_EQ(2, preserved_constraints.and_constraints().constraints_size());
    const auto& c1_check = preserved_constraints.and_constraints().constraints(0).constraint();
    EXPECT_EQ("env", c1_check.key());
    EXPECT_EQ("prod", c1_check.value());
    const auto& c2_not_check =
        preserved_constraints.and_constraints().constraints(1).not_constraints().constraint();
    EXPECT_EQ("version", c2_not_check.key());
    EXPECT_TRUE(c2_not_check.has_exists());
  }
}

} // namespace
} // namespace Config
} // namespace Envoy
