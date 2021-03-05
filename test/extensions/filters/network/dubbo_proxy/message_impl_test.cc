#include "extensions/filters/network/dubbo_proxy/message_impl.h"

#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "hessian2/object.hpp"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

namespace {

TEST(ContextImplTest, ContextImplTest) {
  ContextImpl test;

  test.setHeaderSize(13);
  EXPECT_EQ(13, test.headerSize());

  test.setBodySize(13);
  EXPECT_EQ(13, test.bodySize());

  test.setHeartbeat(true);
  EXPECT_EQ(true, test.isHeartbeat());
}

TEST(RpcInvocationImplAttachmentTest, RpcInvocationImplAttachmentTest) {
  auto map = std::make_unique<RpcInvocationImpl::Attachment::MapObject>();

  map->toMutableUntypedMap()->emplace(std::make_unique<Hessian2::StringObject>("group"),
                                      std::make_unique<Hessian2::StringObject>("fake_group"));
  map->toMutableUntypedMap()->emplace(std::make_unique<Hessian2::StringObject>("fake_key"),
                                      std::make_unique<Hessian2::StringObject>("fake_value"));

  map->toMutableUntypedMap()->emplace(std::make_unique<Hessian2::NullObject>(),
                                      std::make_unique<Hessian2::LongObject>(0));

  map->toMutableUntypedMap()->emplace(std::make_unique<Hessian2::StringObject>("map_key"),
                                      std::make_unique<Hessian2::UntypedMapObject>());

  RpcInvocationImpl::Attachment attachment(std::move(map));

  EXPECT_EQ(4, attachment.attachment().toUntypedMap().value()->size());
  // Only string type key/value pairs will be inserted to header map.
  EXPECT_EQ(2, attachment.headers().size());

  // Test lookup.
  EXPECT_EQ(nullptr, attachment.lookup("map_key"));
  EXPECT_EQ("fake_group", *attachment.lookup("group"));

  // Test remove. Remove a normal string type key/value pair.
  EXPECT_EQ("fake_value", *attachment.lookup("fake_key"));
  attachment.remove("fake_key");
  EXPECT_EQ(nullptr, attachment.lookup("fake_key"));

  EXPECT_EQ(3, attachment.attachment().toUntypedMap().value()->size());
  EXPECT_EQ(1, attachment.headers().size());

  // Test remove. Delete a key/value pair whose value type is map.
  attachment.remove("map_key");
  EXPECT_EQ(2, attachment.attachment().toUntypedMap().value()->size());
  EXPECT_EQ(1, attachment.headers().size());

  // Test insert.
  attachment.insert("test", "test_value");
  EXPECT_EQ(3, attachment.attachment().toUntypedMap().value()->size());
  EXPECT_EQ(2, attachment.headers().size());

  EXPECT_EQ("test_value", *attachment.lookup("test"));
}

TEST(RpcInvocationImplTest, RpcInvocationImplTest) {
  RpcInvocationImpl invo;

  invo.setServiceName("fake_service");
  EXPECT_EQ("fake_service", invo.serviceName());

  invo.setMethodName("fake_method");
  EXPECT_EQ("fake_method", invo.methodName());

  EXPECT_EQ(false, invo.serviceVersion().has_value());
  invo.setServiceVersion("fake_version");
  EXPECT_EQ("fake_version", invo.serviceVersion().value());

  EXPECT_EQ(false, invo.serviceGroup().has_value());
  invo.setServiceGroup("fake_group");
  EXPECT_EQ("fake_group", invo.serviceGroup().value());

  bool set_parameters{false};
  bool set_attachment{false};

  invo.setParametersLazyCallback([&set_parameters]() -> RpcInvocationImpl::ParametersPtr {
    set_parameters = true;
    return std::make_unique<RpcInvocationImpl::Parameters>();
  });

  invo.setAttachmentLazyCallback([&set_attachment]() -> RpcInvocationImpl::AttachmentPtr {
    auto map = std::make_unique<RpcInvocationImpl::Attachment::MapObject>();

    map->toMutableUntypedMap()->emplace(std::make_unique<Hessian2::StringObject>("group"),
                                        std::make_unique<Hessian2::StringObject>("new_fake_group"));

    auto attach = std::make_unique<RpcInvocationImpl::Attachment>(std::move(map));

    set_attachment = true;

    return attach;
  });

  EXPECT_EQ(false, invo.hasParameters());
  EXPECT_EQ(false, invo.hasAttachment());

  // When setting the attachment, the parameters will also be set.
  invo.mutableAttachment();

  EXPECT_EQ(true, set_parameters);
  EXPECT_EQ(true, set_attachment);

  EXPECT_EQ(true, invo.hasParameters());
  EXPECT_EQ(true, invo.hasAttachment());

  EXPECT_EQ("new_fake_group", invo.serviceGroup().value());

  // Reset attachment and parameters.
  set_parameters = false;
  set_attachment = false;
  invo.mutableParameters() = nullptr;
  invo.mutableAttachment() = nullptr;

  invo.mutableParameters();

  EXPECT_EQ(true, set_parameters);
  EXPECT_EQ(false, set_attachment);

  EXPECT_EQ(true, invo.hasParameters());
  EXPECT_EQ(false, invo.hasAttachment());
}

TEST(RpcResultImplTest, RpcResultImplTest) {
  RpcResultImpl result;

  EXPECT_EQ(false, result.hasException());

  result.setException(true);

  EXPECT_EQ(true, result.hasException());
}

} // namespace
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
