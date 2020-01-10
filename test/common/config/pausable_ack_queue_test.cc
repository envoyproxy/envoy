#include "common/config/pausable_ack_queue.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

TEST(PausableAckQueueTest, TestEmpty) {
  PausableAckQueue p;
  EXPECT_EQ(0, p.size());
  EXPECT_TRUE(p.empty());
}

TEST(PausableAckQueueTest, TestPush) {
  PausableAckQueue p;
  p.push(UpdateAck{"bogusnonce", "bogustypeurl"});
  EXPECT_EQ(1, p.size());
  EXPECT_FALSE(p.empty());
  EXPECT_EQ("bogusnonce", p.front().nonce_);
  EXPECT_EQ("bogustypeurl", p.front().type_url_);
}

TEST(PausableAckQueueTest, TestPop) {
  PausableAckQueue p;
  p.push(UpdateAck{"bogusnonce", "bogustypeurl"});
  UpdateAck ack = p.popFront();
  EXPECT_EQ(0, p.size());
  EXPECT_TRUE(p.empty());
  EXPECT_EQ("bogusnonce", ack.nonce_);
  EXPECT_EQ("bogustypeurl", ack.type_url_);
}

TEST(PausableAckQueueTest, TestPauseResume) {
  PausableAckQueue p;
  p.push(UpdateAck{"nonce1", "type1"});
  p.push(UpdateAck{"nonce2", "type2"});
  p.push(UpdateAck{"nonce3", "type1"});
  p.push(UpdateAck{"nonce4", "type2"});
  EXPECT_EQ(4, p.size());
  EXPECT_FALSE(p.empty());

  // pausing 'type1' should make it invisible to the queue
  p.pause("type1");

  // size() doesn't honor pause state, a bit strange but this is by design
  EXPECT_EQ(4, p.size());

  // validate that both front() and popFront() honor pause state
  EXPECT_EQ("nonce2", p.front().nonce_);
  EXPECT_EQ("type2", p.front().type_url_);

  UpdateAck ack = p.popFront();
  EXPECT_EQ("nonce2", ack.nonce_);
  EXPECT_EQ("type2", ack.type_url_);
  EXPECT_EQ(3, p.size());

  // validate that types come back when they're resumed
  p.resume("type1");

  EXPECT_EQ("nonce1", p.front().nonce_);
  EXPECT_EQ("type1", p.front().type_url_);

  p.pause("type1");

  EXPECT_EQ("nonce4", p.front().nonce_);
  EXPECT_EQ("type2", p.front().type_url_);
  p.popFront();

  EXPECT_EQ(2, p.size());

  p.pause("type2");
  EXPECT_TRUE(p.empty());
  // A bit strange but this is by design
  EXPECT_EQ(2, p.size());

  p.resume("type1");
  EXPECT_FALSE(p.empty());

  EXPECT_EQ("nonce1", p.front().nonce_);
  EXPECT_EQ("type1", p.front().type_url_);
  p.popFront();

  EXPECT_EQ("nonce3", p.front().nonce_);
  EXPECT_EQ("type1", p.front().type_url_);
  p.popFront();

  EXPECT_TRUE(p.empty());
  EXPECT_EQ(0, p.size());
}

} // namespace
} // namespace Config
} // namespace Envoy
