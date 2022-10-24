#include "source/extensions/common/dubbo/message_impl.h"

#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "hessian2/object.hpp"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {
namespace {

TEST(RpcRequestImplTest, RpcRequestAttachmentTest) {
  auto map = std::make_unique<RpcRequestImpl::Attachment::Map>();

  map->emplace(std::make_unique<Hessian2::StringObject>("group"),
               std::make_unique<Hessian2::StringObject>("fake_group"));
  map->emplace(std::make_unique<Hessian2::StringObject>("fake_key"),
               std::make_unique<Hessian2::StringObject>("fake_value"));

  map->emplace(std::make_unique<Hessian2::NullObject>(), std::make_unique<Hessian2::LongObject>(0));

  map->emplace(std::make_unique<Hessian2::StringObject>("map_key"),
               std::make_unique<Hessian2::UntypedMapObject>());

  RpcRequestImpl::Attachment attachment(std::move(map), 23333);

  EXPECT_EQ(4, attachment.attachment().toUntypedMap().value().get().size());

  // Test lookup.
  EXPECT_EQ(absl::nullopt, attachment.lookup("map_key"));
  EXPECT_EQ("fake_group", *attachment.lookup("group"));

  EXPECT_FALSE(attachment.attachmentUpdated());

  // Test remove. Remove a normal string type key/value pair.
  EXPECT_EQ("fake_value", *attachment.lookup("fake_key"));
  attachment.remove("fake_key");
  EXPECT_EQ(absl::nullopt, attachment.lookup("fake_key"));

  EXPECT_EQ(3, attachment.attachment().toUntypedMap().value().get().size());

  // Test remove. Delete a key/value pair whose value type is map.
  attachment.remove("map_key");
  EXPECT_EQ(2, attachment.attachment().toUntypedMap().value().get().size());

  // Test insert.
  attachment.insert("test", "test_value");
  EXPECT_EQ(3, attachment.attachment().toUntypedMap().value().get().size());

  EXPECT_EQ("test_value", *attachment.lookup("test"));

  EXPECT_TRUE(attachment.attachmentUpdated());
  EXPECT_EQ(23333, attachment.attachmentOffset());
}

TEST(RpcRequestImplTest, RpcRequestImplTest) {
  RpcRequestImpl request;

  request.setServiceName("fake_service");
  EXPECT_EQ("fake_service", request.serviceName());

  request.setMethodName("fake_method");
  EXPECT_EQ("fake_method", request.methodName());

  request.setServiceVersion("fake_version");
  EXPECT_EQ("fake_version", request.serviceVersion());

  bool set_parameters{false};
  bool set_attachment{false};

  request.setParametersLazyCallback([&set_parameters]() -> RpcRequestImpl::ParametersPtr {
    set_parameters = true;
    return std::make_unique<RpcRequestImpl::Parameters>();
  });

  request.setAttachmentLazyCallback([&set_attachment]() -> RpcRequestImpl::AttachmentPtr {
    auto map = std::make_unique<RpcRequestImpl::Attachment::Map>();

    map->emplace(std::make_unique<Hessian2::StringObject>("group"),
                 std::make_unique<Hessian2::StringObject>("fake_group"));

    auto attach = std::make_unique<RpcRequestImpl::Attachment>(std::move(map), 0);

    set_attachment = true;

    return attach;
  });

  EXPECT_EQ(false, request.hasParameters());
  EXPECT_EQ(false, request.hasAttachment());

  // When parsing attachment, parameters will also be parsed.
  EXPECT_NE(nullptr, request.mutableAttachment());
  request.attachment();
  EXPECT_EQ(true, set_parameters);
  EXPECT_EQ(true, set_attachment);
  EXPECT_EQ(true, request.hasParameters());
  EXPECT_EQ(true, request.hasAttachment());
  EXPECT_EQ("fake_group", request.serviceGroup().value());

  request.setServiceGroup("new_fake_group");
  EXPECT_EQ("new_fake_group", request.serviceGroup().value());

  // If parameters and attachment have values, the callback function will not be executed.
  set_parameters = false;
  set_attachment = false;
  EXPECT_NE(nullptr, request.mutableParameters());
  EXPECT_NE(nullptr, request.mutableAttachment());
  EXPECT_EQ(false, set_parameters);
  EXPECT_EQ(false, set_attachment);

  // Reset attachment and parameters.
  request.mutableParameters() = nullptr;
  request.mutableAttachment() = nullptr;

  // When parsing parameters, attachment will not be parsed.
  request.mutableParameters();
  EXPECT_EQ(true, set_parameters);
  EXPECT_EQ(false, set_attachment);
  EXPECT_EQ(true, request.hasParameters());
  EXPECT_EQ(false, request.hasAttachment());

  request.messageBuffer().add("abcdefg");
  EXPECT_EQ("abcdefg", request.messageBuffer().toString());
}

TEST(RpcResponseImplTest, RpcResponseImplTest) {
  RpcResponseImpl result;

  EXPECT_EQ(false, result.responseType().has_value());
  result.setResponseType(RpcResponseType::ResponseWithValue);
  EXPECT_EQ(true, result.responseType().has_value());
  EXPECT_EQ(RpcResponseType::ResponseWithValue, result.responseType().value());

  EXPECT_EQ(false, result.localRawMessage().has_value());
  result.setLocalRawMessage("abcdefg");
  EXPECT_EQ(true, result.localRawMessage().has_value());
  EXPECT_EQ("abcdefg", result.localRawMessage().value());

  result.messageBuffer().add("abcdefg");
  EXPECT_EQ("abcdefg", result.messageBuffer().toString());
}

} // namespace
} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
