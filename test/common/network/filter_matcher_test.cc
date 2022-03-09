#include "source/common/network/address_impl.h"
#include "source/common/network/filter_matcher.h"

#include "test/mocks/network/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ReturnRef;

namespace Envoy {
namespace Network {
namespace {
struct CallbackHandle {
  std::unique_ptr<Network::MockListenerFilterCallbacks> callback_;
  std::unique_ptr<Network::MockConnectionSocket> socket_;
};
} // namespace
class ListenerFilterMatcherTest : public testing::Test {
public:
  std::unique_ptr<CallbackHandle> createCallbackOnPort(int port) {
    auto handle = std::make_unique<CallbackHandle>();
    handle->socket_ = std::make_unique<MockConnectionSocket>();
    handle->callback_ = std::make_unique<MockListenerFilterCallbacks>();
    handle->socket_->connection_info_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", port));
    EXPECT_CALL(*(handle->callback_), socket()).WillRepeatedly(ReturnRef(*(handle->socket_)));
    return handle;
  }
  envoy::config::listener::v3::ListenerFilterChainMatchPredicate createPortPredicate(int port_start,
                                                                                     int port_end) {
    envoy::config::listener::v3::ListenerFilterChainMatchPredicate pred;
    auto ports = pred.mutable_destination_port_range();
    ports->set_start(port_start);
    ports->set_end(port_end);
    return pred;
  }
};

TEST_F(ListenerFilterMatcherTest, DstPortMatcher) {
  auto pred = createPortPredicate(80, 81);
  auto matcher = ListenerFilterMatcherBuilder::buildListenerFilterMatcher(pred);
  auto handle79 = createCallbackOnPort(79);
  auto handle80 = createCallbackOnPort(80);
  auto handle81 = createCallbackOnPort(81);
  EXPECT_FALSE(matcher->matches(*(handle79->callback_)));
  EXPECT_TRUE(matcher->matches(*(handle80->callback_)));
  EXPECT_FALSE(matcher->matches(*(handle81->callback_)));
}

TEST_F(ListenerFilterMatcherTest, AnyMatdcher) {
  envoy::config::listener::v3::ListenerFilterChainMatchPredicate pred;
  pred.set_any_match(true);
  auto matcher = ListenerFilterMatcherBuilder::buildListenerFilterMatcher(pred);
  auto handle79 = createCallbackOnPort(79);
  auto handle80 = createCallbackOnPort(80);
  auto handle81 = createCallbackOnPort(81);
  EXPECT_TRUE(matcher->matches(*(handle79->callback_)));
  EXPECT_TRUE(matcher->matches(*(handle80->callback_)));
  EXPECT_TRUE(matcher->matches(*(handle81->callback_)));
}

TEST_F(ListenerFilterMatcherTest, NotMatcher) {
  auto pred = createPortPredicate(80, 81);
  envoy::config::listener::v3::ListenerFilterChainMatchPredicate not_pred;
  not_pred.mutable_not_match()->MergeFrom(pred);
  auto matcher = ListenerFilterMatcherBuilder::buildListenerFilterMatcher(not_pred);
  auto handle79 = createCallbackOnPort(79);
  auto handle80 = createCallbackOnPort(80);
  auto handle81 = createCallbackOnPort(81);
  EXPECT_TRUE(matcher->matches(*(handle79->callback_)));
  EXPECT_FALSE(matcher->matches(*(handle80->callback_)));
  EXPECT_TRUE(matcher->matches(*(handle81->callback_)));
}

TEST_F(ListenerFilterMatcherTest, OrMatcher) {
  auto pred80 = createPortPredicate(80, 81);
  auto pred443 = createPortPredicate(443, 444);

  envoy::config::listener::v3::ListenerFilterChainMatchPredicate pred;
  pred.mutable_or_match()->mutable_rules()->Add()->MergeFrom(pred80);
  pred.mutable_or_match()->mutable_rules()->Add()->MergeFrom(pred443);

  auto matcher = ListenerFilterMatcherBuilder::buildListenerFilterMatcher(pred);
  auto handle80 = createCallbackOnPort(80);
  auto handle443 = createCallbackOnPort(443);
  auto handle3306 = createCallbackOnPort(3306);

  EXPECT_FALSE(matcher->matches(*(handle3306->callback_)));
  EXPECT_TRUE(matcher->matches(*(handle80->callback_)));
  EXPECT_TRUE(matcher->matches(*(handle443->callback_)));
}

TEST_F(ListenerFilterMatcherTest, AndMatcher) {
  auto pred80_3306 = createPortPredicate(80, 3306);
  auto pred443_3306 = createPortPredicate(443, 3306);

  envoy::config::listener::v3::ListenerFilterChainMatchPredicate pred;
  pred.mutable_and_match()->mutable_rules()->Add()->MergeFrom(pred80_3306);
  pred.mutable_and_match()->mutable_rules()->Add()->MergeFrom(pred443_3306);

  auto matcher = ListenerFilterMatcherBuilder::buildListenerFilterMatcher(pred);
  auto handle80 = createCallbackOnPort(80);
  auto handle443 = createCallbackOnPort(443);
  auto handle3306 = createCallbackOnPort(3306);

  EXPECT_FALSE(matcher->matches(*(handle3306->callback_)));
  EXPECT_FALSE(matcher->matches(*(handle80->callback_)));
  EXPECT_TRUE(matcher->matches(*(handle443->callback_)));
}
} // namespace Network
} // namespace Envoy
