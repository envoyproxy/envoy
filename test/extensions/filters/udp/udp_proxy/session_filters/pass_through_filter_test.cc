#include <memory>
#include <string>

#include "source/extensions/filters/udp/udp_proxy/session_filters/pass_through_filter.h"

#include "test/extensions/filters/udp/udp_proxy/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {

class UdpSessionPassThroughReadFilterTest : public testing::Test {
public:
  class Filter : public PassThroughReadFilter {
  public:
    ReadFilterCallbacks* readFilterCallbacks() { return read_callbacks_; }
  };

  void initialize() {
    filter_ = std::make_unique<Filter>();
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
  }

  std::unique_ptr<Filter> filter_;
  NiceMock<MockReadFilterCallbacks> filter_callbacks_;
};

// Tests that each method returns Continue filter status.
TEST_F(UdpSessionPassThroughReadFilterTest, AllMethodsAreImplementedTrivially) {
  initialize();
  EXPECT_EQ(&filter_callbacks_, filter_->readFilterCallbacks());
  EXPECT_EQ(SessionFilters::ReadFilterStatus::Continue, filter_->onNewSession());
  Network::UdpRecvData receive_data;
  EXPECT_EQ(SessionFilters::ReadFilterStatus::Continue, filter_->onData(receive_data));
}

class UdpSessionPassThroughWriteFilterTest : public testing::Test {
public:
  class Filter : public PassThroughWriteFilter {
  public:
    WriteFilterCallbacks* writeFilterCallbacks() { return write_callbacks_; }
  };

  void initialize() {
    filter_ = std::make_unique<Filter>();
    filter_->initializeWriteFilterCallbacks(filter_callbacks_);
  }

  std::unique_ptr<Filter> filter_;
  NiceMock<MockWriteFilterCallbacks> filter_callbacks_;
};

// Tests that each method returns Continue filter status.
TEST_F(UdpSessionPassThroughWriteFilterTest, AllMethodsAreImplementedTrivially) {
  initialize();
  EXPECT_EQ(&filter_callbacks_, filter_->writeFilterCallbacks());
  Network::UdpRecvData receive_data;
  EXPECT_EQ(SessionFilters::WriteFilterStatus::Continue, filter_->onWrite(receive_data));
}

class UdpSessionPassThroughFilterTest : public testing::Test {
public:
  class Filter : public PassThroughFilter {
  public:
    ReadFilterCallbacks* readFilterCallbacks() { return read_callbacks_; }
    WriteFilterCallbacks* writeFilterCallbacks() { return write_callbacks_; }
  };

  void initialize() {
    filter_ = std::make_unique<Filter>();
    filter_->initializeReadFilterCallbacks(read_filter_callbacks_);
    filter_->initializeWriteFilterCallbacks(write_filter_callbacks_);
  }

  std::unique_ptr<Filter> filter_;
  NiceMock<MockReadFilterCallbacks> read_filter_callbacks_;
  NiceMock<MockWriteFilterCallbacks> write_filter_callbacks_;
};

// Tests that each method returns Continue filter status.
TEST_F(UdpSessionPassThroughFilterTest, AllMethodsAreImplementedTrivially) {
  initialize();
  EXPECT_EQ(&read_filter_callbacks_, filter_->readFilterCallbacks());
  EXPECT_EQ(&write_filter_callbacks_, filter_->writeFilterCallbacks());
  EXPECT_EQ(SessionFilters::ReadFilterStatus::Continue, filter_->onNewSession());
  Network::UdpRecvData receive_data;
  EXPECT_EQ(SessionFilters::ReadFilterStatus::Continue, filter_->onData(receive_data));
  EXPECT_EQ(SessionFilters::WriteFilterStatus::Continue, filter_->onWrite(receive_data));
}

} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
