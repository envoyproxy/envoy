#include <memory>

#include "source/extensions/filters/network/redis_proxy/control_command_ledger.h"

#include "test/mocks/upstream/host.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace {

using testing::NiceMock;

Upstream::HostConstSharedPtr makeHost() { return std::make_shared<NiceMock<Upstream::MockHost>>(); }

// An in-order ack pops the matching head and returns the generation it acked.
TEST(ControlCommandLedgerTest, InOrderAckPopsHeadAndReturnsGeneration) {
  ControlCommandLedger ledger;
  auto host = makeHost();
  ledger.record(host, "ssubscribe", "a", 7);
  const auto gen = ledger.consumeAck(host, "ssubscribe", "a");
  ASSERT_TRUE(gen.has_value());
  EXPECT_EQ(7U, *gen);
  // Drained: a second ack finds nothing (the map entry was removed with its last command).
  EXPECT_FALSE(ledger.consumeAck(host, "ssubscribe", "a").has_value());
}

// An ack whose (verb, channel) is not the head — an out-of-band / unsolicited push — must NOT pop
// anything, leaving the FIFO intact so the real head still correlates.
TEST(ControlCommandLedgerTest, HeadMismatchLeavesFifoIntact) {
  ControlCommandLedger ledger;
  auto host = makeHost();
  ledger.record(host, "ssubscribe", "a", 1);
  EXPECT_FALSE(ledger.consumeAck(host, "ssubscribe", "b").has_value());   // wrong channel
  EXPECT_FALSE(ledger.consumeAck(host, "sunsubscribe", "a").has_value()); // wrong verb
  const auto gen = ledger.consumeAck(host, "ssubscribe", "a");
  ASSERT_TRUE(gen.has_value());
  EXPECT_EQ(1U, *gen);
}

// Acks must arrive in the exact per-connection send order.
TEST(ControlCommandLedgerTest, FifoOrderAcrossMultipleCommands) {
  ControlCommandLedger ledger;
  auto host = makeHost();
  ledger.record(host, "ssubscribe", "a", 1);
  ledger.record(host, "sunsubscribe", "a");
  ledger.record(host, "ssubscribe", "a", 2);
  EXPECT_EQ(1U, *ledger.consumeAck(host, "ssubscribe", "a"));
  EXPECT_TRUE(ledger.consumeAck(host, "sunsubscribe", "a").has_value());
  EXPECT_EQ(2U, *ledger.consumeAck(host, "ssubscribe", "a"));
}

// takeReply (a non-Push reply / -ERR) pops the oldest command unconditionally and hands it back.
TEST(ControlCommandLedgerTest, TakeReplyPopsHeadUnconditionally) {
  ControlCommandLedger ledger;
  auto host = makeHost();
  ledger.record(host, "ssubscribe", "chan", 5);
  const auto reply = ledger.takeReply(host);
  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ("ssubscribe", reply->verb);
  EXPECT_EQ("chan", reply->channel);
  EXPECT_EQ(5U, reply->generation);
  EXPECT_FALSE(ledger.takeReply(host).has_value()); // drained
}

// clear() drops a host's whole FIFO (its connection died / was retired).
TEST(ControlCommandLedgerTest, ClearDropsHostFifo) {
  ControlCommandLedger ledger;
  auto host = makeHost();
  ledger.record(host, "ssubscribe", "a", 1);
  ledger.record(host, "sunsubscribe", "a");
  ledger.clear(host);
  EXPECT_FALSE(ledger.takeReply(host).has_value());
  EXPECT_FALSE(ledger.consumeAck(host, "ssubscribe", "a").has_value());
}

// A null host (registry test injection without a source host) is a no-op on every operation.
TEST(ControlCommandLedgerTest, NullHostIsNoOp) {
  ControlCommandLedger ledger;
  Upstream::HostConstSharedPtr null_host;
  ledger.record(null_host, "ssubscribe", "a", 1);
  EXPECT_FALSE(ledger.consumeAck(null_host, "ssubscribe", "a").has_value());
  EXPECT_FALSE(ledger.takeReply(null_host).has_value());
  ledger.clear(null_host); // no crash
}

// Each host has its own independent FIFO.
TEST(ControlCommandLedgerTest, PerHostIsolation) {
  ControlCommandLedger ledger;
  auto host1 = makeHost();
  auto host2 = makeHost();
  ledger.record(host1, "ssubscribe", "a", 1);
  ledger.record(host2, "ssubscribe", "a", 2);
  EXPECT_EQ(2U, *ledger.consumeAck(host2, "ssubscribe", "a"));
  EXPECT_EQ(1U, *ledger.consumeAck(host1, "ssubscribe", "a"));
}

// clearAll() drops every host's FIFO (the registry's clear() on cluster removal).
TEST(ControlCommandLedgerTest, ClearAllDropsEveryHost) {
  ControlCommandLedger ledger;
  auto host1 = makeHost();
  auto host2 = makeHost();
  ledger.record(host1, "ssubscribe", "a", 1);
  ledger.record(host2, "ssubscribe", "b", 2);
  ledger.clearAll();
  EXPECT_FALSE(ledger.takeReply(host1).has_value());
  EXPECT_FALSE(ledger.takeReply(host2).has_value());
}

} // namespace
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
