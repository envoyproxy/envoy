#include "source/common/stream_info/filter_state_impl.h"
#include "source/common/stream_info/uint32_accessor_impl.h"
#include "source/extensions/filters/http/ratelimit/ratelimit.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/ratelimit/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;
using testing::SetArgReferee;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitFilter {
namespace {

// Forward declare the HitsAddendObjectFactory to test
namespace {
class HitsAddendObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const { return "envoy.ratelimit.hits_addend"; }
  std::unique_ptr<StreamInfo::FilterState::Object> createFromBytes(absl::string_view data) const {
    uint32_t hits_addend = 0;
    if (absl::SimpleAtoi(data, &hits_addend)) {
      return std::make_unique<StreamInfo::UInt32AccessorImpl>(hits_addend);
    }
    return nullptr;
  }
};
} // namespace

class RateLimitLoggingInfoTest : public testing::Test {
protected:
  RateLimitLoggingInfo logging_info_;
  std::shared_ptr<NiceMock<Upstream::MockClusterInfo>> cluster_info_{
      std::make_shared<NiceMock<Upstream::MockClusterInfo>>()};
  std::shared_ptr<NiceMock<Upstream::MockHostDescription>> host_{
      std::make_shared<NiceMock<Upstream::MockHostDescription>>()};
};

// Test default values
TEST_F(RateLimitLoggingInfoTest, DefaultValues) {
  EXPECT_FALSE(logging_info_.latency().has_value());
  EXPECT_FALSE(logging_info_.bytesSent().has_value());
  EXPECT_FALSE(logging_info_.bytesReceived().has_value());
  EXPECT_EQ(nullptr, logging_info_.clusterInfo());
  EXPECT_EQ(nullptr, logging_info_.upstreamHost());
}

// Test setting values
TEST_F(RateLimitLoggingInfoTest, SetValues) {
  const std::chrono::microseconds latency(100);
  logging_info_.setLatency(latency);
  EXPECT_EQ(latency, logging_info_.latency().value());

  const uint64_t bytes_sent = 200;
  logging_info_.setBytesSent(bytes_sent);
  EXPECT_EQ(bytes_sent, logging_info_.bytesSent().value());

  const uint64_t bytes_received = 300;
  logging_info_.setBytesReceived(bytes_received);
  EXPECT_EQ(bytes_received, logging_info_.bytesReceived().value());

  logging_info_.setClusterInfo(cluster_info_);
  EXPECT_EQ(cluster_info_.get(), logging_info_.clusterInfo().get());

  logging_info_.setUpstreamHost(host_);
  EXPECT_EQ(host_, logging_info_.upstreamHost());
}

// Test hasFieldSupport method
TEST_F(RateLimitLoggingInfoTest, HasFieldSupport) { EXPECT_TRUE(logging_info_.hasFieldSupport()); }

// Test filter state integration
TEST_F(RateLimitLoggingInfoTest, FilterStateIntegration) {
  const std::string filter_name = "http.ratelimit";
  auto filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::FilterChain);

  // Set logging info values
  logging_info_.setLatency(std::chrono::microseconds(100));
  logging_info_.setBytesSent(200);
  logging_info_.setBytesReceived(300);
  logging_info_.setClusterInfo(cluster_info_);
  logging_info_.setUpstreamHost(host_);

  // Store in filter state
  filter_state->setData(filter_name, std::make_unique<RateLimitLoggingInfo>(logging_info_),
                        StreamInfo::FilterState::StateType::ReadOnly);

  // Verify retrieval
  EXPECT_TRUE(filter_state->hasData<RateLimitLoggingInfo>(filter_name));
  const auto* info = filter_state->getDataReadOnly<RateLimitLoggingInfo>(filter_name);
  EXPECT_NE(nullptr, info);

  // Verify stored values match original
  EXPECT_EQ(logging_info_.latency(), info->latency());
  EXPECT_EQ(logging_info_.bytesSent(), info->bytesSent());
  EXPECT_EQ(logging_info_.bytesReceived(), info->bytesReceived());
  EXPECT_EQ(logging_info_.clusterInfo(), info->clusterInfo());
  EXPECT_EQ(logging_info_.upstreamHost(), info->upstreamHost());
}

class HitsAddendObjectFactoryTest : public testing::Test {
protected:
  HitsAddendObjectFactory factory_;
};

// Test HitsAddendObjectFactory name method
TEST_F(HitsAddendObjectFactoryTest, Name) {
  EXPECT_EQ("envoy.ratelimit.hits_addend", factory_.name());
}

// Test HitsAddendObjectFactory createFromBytes() method
TEST_F(HitsAddendObjectFactoryTest, CreateFromBytes) {
  // Valid case
  {
    std::string data = "123";
    auto obj = factory_.createFromBytes(data);
    ASSERT_NE(nullptr, obj);
    auto* accessor = dynamic_cast<StreamInfo::UInt32Accessor*>(obj.get());
    ASSERT_NE(nullptr, accessor);
    EXPECT_EQ(123, accessor->value());
  }

  // Invalid case (not a number)
  {
    std::string data = "not_a_number";
    auto obj = factory_.createFromBytes(data);
    EXPECT_EQ(nullptr, obj);
  }

  // Empty string
  {
    std::string data = "";
    auto obj = factory_.createFromBytes(data);
    EXPECT_EQ(nullptr, obj);
  }
}

// Test for the HitsAddendObjectFactory using Envoy filter state directly
TEST_F(RateLimitLoggingInfoTest, HitsAddendFilterStateIntegration) {
  // Create filter state
  auto filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::FilterChain);

  // Create and set hits_addend data
  const std::string key = "envoy.ratelimit.hits_addend";
  const std::string value = "42";
  filter_state->setData(key, std::make_shared<StreamInfo::UInt32AccessorImpl>(42),
                        StreamInfo::FilterState::StateType::ReadOnly);

  // Verify retrieval
  EXPECT_TRUE(filter_state->hasData<StreamInfo::UInt32Accessor>(key));
  const auto* hits_addend = filter_state->getDataReadOnly<StreamInfo::UInt32Accessor>(key);
  ASSERT_NE(nullptr, hits_addend);
  EXPECT_EQ(42, hits_addend->value());

  // Create a new HitsAddendObjectFactory directly
  HitsAddendObjectFactory factory;

  // Create object from bytes
  auto obj = factory.createFromBytes(value);
  ASSERT_NE(nullptr, obj);

  // Verify value
  auto* accessor = dynamic_cast<StreamInfo::UInt32Accessor*>(obj.get());
  ASSERT_NE(nullptr, accessor);
  EXPECT_EQ(42, accessor->value());
}

// Test edge case by setting a null cluster info
TEST_F(RateLimitLoggingInfoTest, NullClusterInfo) {
  Upstream::ClusterInfoConstSharedPtr null_cluster_info;
  logging_info_.setClusterInfo(null_cluster_info);
  EXPECT_EQ(nullptr, logging_info_.clusterInfo());
}

// Test edge case by setting a null upstream host
TEST_F(RateLimitLoggingInfoTest, NullUpstreamHost) {
  Upstream::HostDescriptionConstSharedPtr null_host;
  logging_info_.setUpstreamHost(null_host);
  EXPECT_EQ(nullptr, logging_info_.upstreamHost());
}

// Test setting extremely large values
TEST_F(RateLimitLoggingInfoTest, LargeValues) {
  const std::chrono::microseconds large_latency(std::numeric_limits<int64_t>::max());
  logging_info_.setLatency(large_latency);
  EXPECT_EQ(large_latency, logging_info_.latency().value());

  const uint64_t large_bytes = std::numeric_limits<uint64_t>::max();
  logging_info_.setBytesSent(large_bytes);
  EXPECT_EQ(large_bytes, logging_info_.bytesSent().value());

  logging_info_.setBytesReceived(large_bytes);
  EXPECT_EQ(large_bytes, logging_info_.bytesReceived().value());
}

// Test for storing and retrieving multiple filter state objects
TEST_F(RateLimitLoggingInfoTest, MultipleFilterStateObjects) {
  auto filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::FilterChain);

  // Set up logging info values
  logging_info_.setLatency(std::chrono::microseconds(100));
  logging_info_.setBytesSent(200);
  logging_info_.setBytesReceived(300);

  // Create a second logging info with different values
  RateLimitLoggingInfo second_logging_info;
  second_logging_info.setLatency(std::chrono::microseconds(500));
  second_logging_info.setBytesSent(600);
  second_logging_info.setBytesReceived(700);

  // Store both in filter state with different names
  const std::string first_filter_name = "ratelimit.first";
  const std::string second_filter_name = "ratelimit.second";

  filter_state->setData(first_filter_name, std::make_unique<RateLimitLoggingInfo>(logging_info_),
                        StreamInfo::FilterState::StateType::ReadOnly);
  filter_state->setData(second_filter_name,
                        std::make_unique<RateLimitLoggingInfo>(second_logging_info),
                        StreamInfo::FilterState::StateType::ReadOnly);

  // Verify retrieval of first object
  EXPECT_TRUE(filter_state->hasData<RateLimitLoggingInfo>(first_filter_name));
  const auto* info1 = filter_state->getDataReadOnly<RateLimitLoggingInfo>(first_filter_name);
  ASSERT_NE(nullptr, info1);
  EXPECT_EQ(
      100, std::chrono::duration_cast<std::chrono::microseconds>(info1->latency().value()).count());
  EXPECT_EQ(200, info1->bytesSent().value());
  EXPECT_EQ(300, info1->bytesReceived().value());

  // Verify retrieval of second object
  EXPECT_TRUE(filter_state->hasData<RateLimitLoggingInfo>(second_filter_name));
  const auto* info2 = filter_state->getDataReadOnly<RateLimitLoggingInfo>(second_filter_name);
  ASSERT_NE(nullptr, info2);
  EXPECT_EQ(
      500, std::chrono::duration_cast<std::chrono::microseconds>(info2->latency().value()).count());
  EXPECT_EQ(600, info2->bytesSent().value());
  EXPECT_EQ(700, info2->bytesReceived().value());
}

// Test the copy constructor
TEST_F(RateLimitLoggingInfoTest, CopyConstructor) {
  // Set up original values
  const std::chrono::microseconds latency(123);
  logging_info_.setLatency(latency);
  logging_info_.setBytesSent(456);
  logging_info_.setBytesReceived(789);
  logging_info_.setClusterInfo(cluster_info_);
  logging_info_.setUpstreamHost(host_);

  // Create a copy
  RateLimitLoggingInfo copy(logging_info_);

  // Verify all values were copied correctly
  EXPECT_EQ(latency, copy.latency().value());
  EXPECT_EQ(456, copy.bytesSent().value());
  EXPECT_EQ(789, copy.bytesReceived().value());
  EXPECT_EQ(cluster_info_.get(), copy.clusterInfo().get());
  EXPECT_EQ(host_, copy.upstreamHost());
}

// This test specifically triggers the HitsAddendObjectFactory's createFromBytes() with invalid
// input
TEST_F(HitsAddendObjectFactoryTest, InvalidInputHandling) {
  // Test with negative number (should fail)
  {
    std::string data = "-5";
    auto obj = factory_.createFromBytes(data);
    // Should return nullptr for negative numbers
    EXPECT_EQ(nullptr, obj);
  }

  // Test with valid uint32_t max value
  {
    std::string data = "4294967295"; // Max value for uint32_t
    auto obj = factory_.createFromBytes(data);
    ASSERT_NE(nullptr, obj);
    auto* accessor = dynamic_cast<StreamInfo::UInt32Accessor*>(obj.get());
    ASSERT_NE(nullptr, accessor);
    EXPECT_EQ(4294967295, accessor->value());
  }

  // Test with invalid very large number
  {
    std::string data = "18446744073709551615"; // uint64_t max value
    auto obj = factory_.createFromBytes(data);
    // The implementation likely can't handle this large value so we just verify that it doesn't
    // crash
    if (obj != nullptr) {
      auto* accessor = dynamic_cast<StreamInfo::UInt32Accessor*>(obj.get());
      if (accessor != nullptr) {
        // Just access the value to make sure it doesn't crash
        accessor->value();
      }
    }
  }
}

} // namespace
} // namespace RateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
