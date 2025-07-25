#include "source/extensions/filters/network/generic_proxy/tracing.h"

#include "test/extensions/filters/network/generic_proxy/fake_codec.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace {

TEST(TraceContextBridgeTest, TraceContextBridgeTest) {

  FakeStreamCodecFactory::FakeRequest request;
  request.protocol_ = "test_protocol";
  request.host_ = "test_host";
  request.method_ = "test_method";
  request.path_ = "test_path";

  request.data_ = {{"key1", "value1"}, {"key2", "value2"}};

  TraceContextBridge bridge(request);

  EXPECT_EQ(bridge.protocol(), "test_protocol");
  EXPECT_EQ(bridge.host(), "test_host");
  EXPECT_EQ(bridge.method(), "test_method");
  EXPECT_EQ(bridge.path(), "test_path");

  EXPECT_EQ(bridge.get("key1"), "value1");
  EXPECT_EQ(bridge.get("key2"), "value2");
  EXPECT_FALSE(bridge.get("key3").has_value());

  bridge.set("key3", "value3");
  EXPECT_EQ(bridge.get("key3"), "value3");
  EXPECT_EQ(request.data_.at("key3"), "value3");

  bridge.remove("key3");
  EXPECT_FALSE(bridge.get("key3").has_value());

  size_t count = 0;
  bridge.forEach([&count](absl::string_view, absl::string_view) {
    count++;
    return true;
  });

  EXPECT_EQ(count, 2);
}

} // namespace
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
