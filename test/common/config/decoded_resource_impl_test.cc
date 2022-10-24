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
  ProtobufWkt::Any some_opaque_resource;
  some_opaque_resource.set_type_url("some_type_url");

  {
    EXPECT_CALL(resource_decoder, decodeResource(ProtoEq(some_opaque_resource)))
        .WillOnce(InvokeWithoutArgs(
            []() -> ProtobufTypes::MessagePtr { return std::make_unique<ProtobufWkt::Empty>(); }));
    EXPECT_CALL(resource_decoder, resourceName(ProtoEq(ProtobufWkt::Empty())))
        .WillOnce(Return("some_name"));
    auto decoded_resource =
        DecodedResourceImpl::fromResource(resource_decoder, some_opaque_resource, "foo");
    EXPECT_EQ("some_name", decoded_resource->name());
    EXPECT_TRUE(decoded_resource->aliases().empty());
    EXPECT_EQ("foo", decoded_resource->version());
    EXPECT_THAT(decoded_resource->resource(), ProtoEq(ProtobufWkt::Empty()));
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
            []() -> ProtobufTypes::MessagePtr { return std::make_unique<ProtobufWkt::Empty>(); }));
    EXPECT_CALL(resource_decoder, resourceName(ProtoEq(ProtobufWkt::Empty()))).Times(0);
    DecodedResourceImpl decoded_resource(resource_decoder, resource_wrapper);
    EXPECT_EQ("real_name", decoded_resource.name());
    EXPECT_EQ((std::vector<std::string>{"bar", "baz"}), decoded_resource.aliases());
    EXPECT_EQ("foo", decoded_resource.version());
    EXPECT_THAT(decoded_resource.resource(), ProtoEq(ProtobufWkt::Empty()));
    EXPECT_TRUE(decoded_resource.hasResource());
  }

  {
    envoy::service::discovery::v3::Resource resource_wrapper;
    resource_wrapper.set_name("real_name");
    resource_wrapper.set_version("foo");
    resource_wrapper.add_aliases("bar");
    resource_wrapper.add_aliases("baz");
    EXPECT_CALL(resource_decoder, decodeResource(ProtoEq(ProtobufWkt::Any())))
        .WillOnce(InvokeWithoutArgs(
            []() -> ProtobufTypes::MessagePtr { return std::make_unique<ProtobufWkt::Empty>(); }));
    EXPECT_CALL(resource_decoder, resourceName(_)).Times(0);
    DecodedResourceImpl decoded_resource(resource_decoder, resource_wrapper);
    EXPECT_EQ("real_name", decoded_resource.name());
    EXPECT_EQ((std::vector<std::string>{"bar", "baz"}), decoded_resource.aliases());
    EXPECT_EQ("foo", decoded_resource.version());
    EXPECT_THAT(decoded_resource.resource(), ProtoEq(ProtobufWkt::Empty()));
    EXPECT_FALSE(decoded_resource.hasResource());
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
            []() -> ProtobufTypes::MessagePtr { return std::make_unique<ProtobufWkt::Empty>(); }));
    EXPECT_CALL(resource_decoder, resourceName(ProtoEq(ProtobufWkt::Empty()))).Times(0);
    DecodedResourceImplPtr decoded_resource =
        DecodedResourceImpl::fromResource(resource_decoder, resource_wrapper);
    EXPECT_EQ("real_name", decoded_resource->name());
    EXPECT_EQ((std::vector<std::string>{"bar", "baz"}), decoded_resource->aliases());
    EXPECT_EQ("foo", decoded_resource->version());
    EXPECT_THAT(decoded_resource->resource(), ProtoEq(ProtobufWkt::Empty()));
    EXPECT_TRUE(decoded_resource->hasResource());
  }

  {
    auto message = std::make_unique<ProtobufWkt::Empty>();
    DecodedResourceImpl decoded_resource(std::move(message), "real_name", {"bar", "baz"}, "foo");
    EXPECT_EQ("real_name", decoded_resource.name());
    EXPECT_EQ((std::vector<std::string>{"bar", "baz"}), decoded_resource.aliases());
    EXPECT_EQ("foo", decoded_resource.version());
    EXPECT_THAT(decoded_resource.resource(), ProtoEq(ProtobufWkt::Empty()));
    EXPECT_TRUE(decoded_resource.hasResource());
  }
}

} // namespace
} // namespace Config
} // namespace Envoy
