#include "common/network/utility.h"

#include "extensions/filters/network/original_src/original_src.h"
#include "extensions/filters/network/original_src/original_src_socket_option.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Exactly;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace OriginalSrc {

class OriginalSrcTest : public testing::Test {
public:
  std::unique_ptr<OriginalSrcFilter> makeDefaultFilter() {
    return std::make_unique<OriginalSrcFilter>();
  }

  void setAddressToReturn(const std::string& address) {
    callbacks_.connection_.remote_address_ = Network::Utility::resolveUrl(address);
  }

protected:
  MockBuffer buffer_;
  NiceMock<Network::MockReadFilterCallbacks> callbacks_;
};

// test that we return from onData with no changes to teh buffer
TEST_F(OriginalSrcTest, onDataDoesNothing) {
  auto filter = makeDefaultFilter();
  EXPECT_EQ(filter->onData(buffer_, false), Network::FilterStatus::Continue);
  EXPECT_EQ(filter->onData(buffer_, true), Network::FilterStatus::Continue);
}

// test that if we do not have callbacks yet we just skip on new connection
TEST_F(OriginalSrcTest, onNewConnectionNoCallbacksSkip) {
  auto filter = makeDefaultFilter();
  EXPECT_EQ(filter->onNewConnection(), Network::FilterStatus::Continue);
}

TEST_F(OriginalSrcTest, onNewConnectionUnixSocketSkips) {
  auto filter = makeDefaultFilter();
  filter->initializeReadFilterCallbacks(callbacks_);
  setAddressToReturn("unix://domain.socket");
  EXPECT_CALL(callbacks_.connection_, socketOptions()).Times(0);
  EXPECT_EQ(filter->onNewConnection(), Network::FilterStatus::Continue);
}

TEST_F(OriginalSrcTest, onNewConnectionIpv4AddressAddsOption) {
  auto filter = makeDefaultFilter();
  filter->initializeReadFilterCallbacks(callbacks_);
  setAddressToReturn("tcp://1.2.3.4:0");
  Network::Socket::OptionsSharedPtr options{std::make_shared<Network::Socket::Options>()};
  EXPECT_CALL(callbacks_.connection_, socketOptions()).WillOnce(ReturnRef(options));

  EXPECT_EQ(filter->onNewConnection(), Network::FilterStatus::Continue);
  ASSERT_GE(options->size(), 1);
  auto option = (*options)[0];
  EXPECT_NE(dynamic_cast<const Network::OriginalSrcSocketOption*>(option.get()), nullptr);
}

TEST_F(OriginalSrcTest, onNewConnectionIpv4AddressUsesCorrectAddress) {
  auto filter = makeDefaultFilter();
  filter->initializeReadFilterCallbacks(callbacks_);
  setAddressToReturn("tcp://1.2.3.4:0");
  Network::Socket::OptionsSharedPtr options{std::make_shared<Network::Socket::Options>()};
  EXPECT_CALL(callbacks_.connection_, socketOptions()).WillOnce(ReturnRef(options));

  filter->onNewConnection();
  auto option = (*options)[0];
  std::vector<uint8_t> key;
  option->hashKey(key);
  std::vector<uint8_t> expected_key = {Network::OriginalSrcSocketOption::IPV4_KEY};
  // add the padding
  expected_key.insert(expected_key.end(), 12, 0);
  expected_key.insert(expected_key.end(), {1, 2, 3, 4});

  EXPECT_EQ(key, expected_key);
}
} // namespace OriginalSrc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
