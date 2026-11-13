#include <limits>

#include "envoy/tcp/conn_pool.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/common/wasm/ext/redis_call.pb.h"
#include "source/extensions/common/wasm/wasm.h"
#include "source/extensions/filters/network/common/redis/codec_impl.h"

#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/connection.h"
#include "test/mocks/tcp/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

namespace CommonRedis = ::Envoy::Extensions::NetworkFilters::Common::Redis;

class RedisCallForeignTest : public testing::Test, public Event::TestUsingSimulatedTime {
public:
  static constexpr absl::string_view kCluster = "redis_cluster";

  RedisCallForeignTest()
      : api_(Api::createApiForTest(stats_store_, simTime())),
        dispatcher_(api_->allocateDispatcher("wasm_test")), scope_(stats_store_.createScope("")) {
    envoy::extensions::wasm::v3::PluginConfig plugin_config;
    // ContextBase::onQueueReady() (exercised via deliverViaOnQueueReady() below) no-ops unless
    // wasm_->isFailed() is false, which requires a real (if otherwise unused) WasmVm -- otherwise
    // WasmBase's constructor immediately sets failed_ = FailState::UnableToCreateVm. The null
    // runtime provides exactly that: a VM shell with no bytecode/module, since this fixture never
    // loads one.
    plugin_config.mutable_vm_config()->set_runtime("envoy.wasm.runtime.null");
    plugin_ = std::make_shared<Plugin>(plugin_config, local_info_);
    wasm_ = std::make_shared<Wasm>(plugin_->wasmConfig(), "", scope_, *api_, cluster_manager_,
                                   *dispatcher_);
    context_ = std::make_unique<Context>(wasm_.get(), plugin_);
    proxy_wasm::current_context_ = context_.get();

    ON_CALL(cluster_manager_, getThreadLocalCluster(absl::string_view(kCluster)))
        .WillByDefault(testing::Return(&thread_local_cluster_));
  }

  ~RedisCallForeignTest() override { proxy_wasm::current_context_ = nullptr; }

  // Serializes RedisCallArguments and invokes the real "redis_call" foreign function, mirroring
  // what a Wasm module does via proxy_call_foreign_function. timeout_millis is mandatory (see
  // Context::redisCall()); defaults to an arbitrary valid value for tests that don't care.
  WasmResult callRedis(absl::string_view cluster, const std::vector<std::string>& command_args,
                       std::string* result_buffer, uint32_t timeout_millis = 1000) {
    envoy::source::extensions::common::wasm::RedisCallArguments args;
    args.set_cluster(std::string(cluster));
    for (const auto& arg : command_args) {
      args.add_command_args(arg);
    }
    args.set_timeout_millis(timeout_millis);
    std::string serialized;
    std::ignore = args.SerializeToString(&serialized);
    auto function = proxy_wasm::getForeignFunction("redis_call");
    return function(*wasm_, serialized, [result_buffer](size_t size) -> void* {
      result_buffer->resize(size);
      return result_buffer->data();
    });
  }

  // Encodes a RESP2 simple string reply, exactly what onPoolReady's peer would write back onto
  // the connection, for feeding into the captured Tcp::ConnectionPool::UpstreamCallbacks.
  static ::Envoy::Buffer::OwnedImpl simpleStringReply(absl::string_view s) {
    CommonRedis::RespValue value;
    value.type(CommonRedis::RespType::SimpleString);
    value.asString() = std::string(s);
    ::Envoy::Buffer::OwnedImpl buffer;
    CommonRedis::EncoderImpl().encode(value, buffer);
    return buffer;
  }

  // Encodes an arbitrary RESP2 reply the same way simpleStringReply() does above, for Finding
  // #5's result-byte-contract tests (integer, null, binary bulk string, error, nested array).
  static ::Envoy::Buffer::OwnedImpl encodedReply(const CommonRedis::RespValue& value) {
    ::Envoy::Buffer::OwnedImpl buffer;
    CommonRedis::EncoderImpl().encode(value, buffer);
    return buffer;
  }

  // Issues a call, drives the pool to onPoolReady, and returns the UpstreamCallbacks the handler
  // registered via addUpstreamCallbacks so the test can simulate the peer's reply/close.
  Tcp::ConnectionPool::UpstreamCallbacks* startCallAndConnect(absl::string_view cluster,
                                                              const std::vector<std::string>& args,
                                                              std::string* result_buffer,
                                                              uint32_t timeout_millis = 1000) {
    EXPECT_EQ(callRedis(cluster, args, result_buffer, timeout_millis), WasmResult::Ok);
    Tcp::ConnectionPool::UpstreamCallbacks* captured = nullptr;
    EXPECT_CALL(*thread_local_cluster_.tcp_conn_pool_.connection_data_, addUpstreamCallbacks(_))
        .WillOnce([&captured](Tcp::ConnectionPool::UpstreamCallbacks& cb) { captured = &cb; });
    EXPECT_CALL(connection_, write(_, false));
    thread_local_cluster_.tcp_conn_pool_.poolReady(connection_);
    return captured;
  }

  // Exercises the real ContextBase::onQueueReady -> Wasm::on_queue_ready_ dispatch (wired via
  // setOnQueueReadyForTesting) that a real Wasm module's proxy_on_queue_ready export would
  // receive, dequeuing from *within* that callback -- mirroring how a module actually consumes a
  // shared queue, rather than calling dequeueSharedQueue directly without ever exercising
  // onQueueReady (the gap this seam exists to close).
  //
  // In production, SharedQueue::enqueue() (vendored src/shared_queue.cc) reaches this same
  // ContextBase::onQueueReady call after a cross-thread redelivery hop through proxy-wasm's own
  // thread-local VM registry (getThreadLocalWasm), which is only populated by bringing up a real
  // Wasm VM (see test/test_common/wasm_base.h) -- too heavy for this foreign-function-only
  // fixture, which calls redis_call() without any VM/module at all. So this drives
  // ContextBase::onQueueReady directly on `root`, which is what that hop would have called
  // getContext(context_id)->onQueueReady(token) into; the hop itself is vendored-library plumbing
  // already covered by proxy-wasm-cpp-host's own shared_queue_test.cc.
  envoy::source::extensions::common::wasm::RedisCallResult
  deliverViaOnQueueReady(Wasm& wasm, Context& root, uint32_t token) {
    uint32_t observed_context_id = 0;
    uint32_t observed_token = 0;
    std::string queue_data;
    WasmResult dequeue_result = WasmResult::NotFound;
    wasm.setOnQueueReadyForTesting(
        [&](proxy_wasm::ContextBase* ctx, proxy_wasm::Word id, proxy_wasm::Word tok) {
          observed_context_id = id.u32();
          observed_token = tok.u32();
          dequeue_result = static_cast<Context*>(ctx)->dequeueSharedQueue(tok.u32(), &queue_data);
        });
    root.onQueueReady(token);
    EXPECT_EQ(observed_context_id, root.id());
    EXPECT_EQ(observed_token, token);
    EXPECT_EQ(dequeue_result, WasmResult::Ok);
    envoy::source::extensions::common::wasm::RedisCallResult result;
    EXPECT_TRUE(result.ParseFromString(queue_data));
    return result;
  }

protected:
  Stats::IsolatedStoreImpl stats_store_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Stats::ScopeSharedPtr scope_;
  testing::NiceMock<Upstream::MockClusterManager> cluster_manager_;
  testing::NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster_;
  testing::NiceMock<Network::MockClientConnection> connection_;
  testing::NiceMock<LocalInfo::MockLocalInfo> local_info_;
  std::shared_ptr<Plugin> plugin_;
  std::shared_ptr<Wasm> wasm_;
  std::unique_ptr<Context> context_;
};

using testing::_;

TEST_F(RedisCallForeignTest, SuccessRoundTrip) {
  std::string result_buffer;
  auto* upstream_callbacks = startCallAndConnect(kCluster, {"PING"}, &result_buffer);
  ASSERT_NE(upstream_callbacks, nullptr);

  envoy::source::extensions::common::wasm::RedisCallStartResult start_result;
  ASSERT_TRUE(start_result.ParseFromString(result_buffer));

  // Nothing has been delivered yet: the reply hasn't arrived.
  std::string queue_data;
  EXPECT_EQ(context_->dequeueSharedQueue(start_result.queue_token(), &queue_data),
            WasmResult::Empty);

  auto reply = simpleStringReply("PONG");
  upstream_callbacks->onUpstreamData(reply, false);
  // onRespValue defers completion via dispatcher().post(); flush it.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  auto redis_result = deliverViaOnQueueReady(*wasm_, *context_, start_result.queue_token());
  EXPECT_EQ(redis_result.status(), envoy::source::extensions::common::wasm::RedisCallResult::Ok);
  EXPECT_THAT(redis_result.response(), testing::HasSubstr("PONG"));
}

// Asserts the literal RESP2 bytes written upstream, catching wrong-protocol regressions (bad
// length prefix, missing CRLF, wrong element count) that status/content-only tests would miss.
TEST_F(RedisCallForeignTest, WriteSerializesSingleArgCommandAsExactResp2Bytes) {
  std::string result_buffer;
  ASSERT_EQ(callRedis(kCluster, {"PING"}, &result_buffer), WasmResult::Ok);

  std::string written;
  EXPECT_CALL(*thread_local_cluster_.tcp_conn_pool_.connection_data_, addUpstreamCallbacks(_));
  EXPECT_CALL(connection_, write(_, false))
      .WillOnce([&written](::Envoy::Buffer::Instance& data, bool) { written = data.toString(); });
  thread_local_cluster_.tcp_conn_pool_.poolReady(connection_);

  EXPECT_EQ(written, "*1\r\n$4\r\nPING\r\n");
}

TEST_F(RedisCallForeignTest, WriteSerializesMultiArgCommandAsExactResp2Bytes) {
  std::string result_buffer;
  ASSERT_EQ(callRedis(kCluster, {"SET", "foo", "bar"}, &result_buffer), WasmResult::Ok);

  std::string written;
  EXPECT_CALL(*thread_local_cluster_.tcp_conn_pool_.connection_data_, addUpstreamCallbacks(_));
  EXPECT_CALL(connection_, write(_, false))
      .WillOnce([&written](::Envoy::Buffer::Instance& data, bool) { written = data.toString(); });
  thread_local_cluster_.tcp_conn_pool_.poolReady(connection_);

  EXPECT_EQ(written, "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n");
}

// Binary-safe: an argument containing embedded NUL/CR/LF bytes must come through length-prefixed
// and byte-exact, not truncated at the first embedded NUL (as a naive C-string-based path would)
// or corrupted by treating the embedded CR/LF as framing.
TEST_F(RedisCallForeignTest, WriteSerializesBinaryArgWithEmbeddedNulAndCrlfAsExactResp2Bytes) {
  std::string binary_arg = std::string("a\0b\r\nc", 6);
  std::string result_buffer;
  ASSERT_EQ(callRedis(kCluster, {"SET", "key", binary_arg}, &result_buffer), WasmResult::Ok);

  std::string written;
  EXPECT_CALL(*thread_local_cluster_.tcp_conn_pool_.connection_data_, addUpstreamCallbacks(_));
  EXPECT_CALL(connection_, write(_, false))
      .WillOnce([&written](::Envoy::Buffer::Instance& data, bool) { written = data.toString(); });
  thread_local_cluster_.tcp_conn_pool_.poolReady(connection_);

  std::string expected = "*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$6\r\n" + binary_arg + "\r\n";
  EXPECT_EQ(written, expected);
}

// The queued result is a RESP2 re-encoding of what the decoder parsed (see
// Context::onRedisCallSuccess), not the original wire bytes -- assert the exact re-encoded bytes
// per reply shape, not just a substring match.
TEST_F(RedisCallForeignTest, ResultBytesForIntegerReplyAreExactResp2) {
  std::string result_buffer;
  auto* upstream_callbacks = startCallAndConnect(kCluster, {"INCR", "counter"}, &result_buffer);
  ASSERT_NE(upstream_callbacks, nullptr);

  CommonRedis::RespValue value;
  value.type(CommonRedis::RespType::Integer);
  value.asInteger() = 42;
  auto reply = encodedReply(value);
  upstream_callbacks->onUpstreamData(reply, false);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  envoy::source::extensions::common::wasm::RedisCallStartResult start_result;
  ASSERT_TRUE(start_result.ParseFromString(result_buffer));
  auto redis_result = deliverViaOnQueueReady(*wasm_, *context_, start_result.queue_token());
  EXPECT_EQ(redis_result.status(), envoy::source::extensions::common::wasm::RedisCallResult::Ok);
  EXPECT_EQ(redis_result.response(), ":42\r\n");
}

TEST_F(RedisCallForeignTest, ResultBytesForNullReplyAreExactResp2) {
  std::string result_buffer;
  auto* upstream_callbacks = startCallAndConnect(kCluster, {"GET", "missing"}, &result_buffer);
  ASSERT_NE(upstream_callbacks, nullptr);

  CommonRedis::RespValue value;
  value.type(CommonRedis::RespType::Null);
  auto reply = encodedReply(value);
  upstream_callbacks->onUpstreamData(reply, false);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  envoy::source::extensions::common::wasm::RedisCallStartResult start_result;
  ASSERT_TRUE(start_result.ParseFromString(result_buffer));
  auto redis_result = deliverViaOnQueueReady(*wasm_, *context_, start_result.queue_token());
  EXPECT_EQ(redis_result.status(), envoy::source::extensions::common::wasm::RedisCallResult::Ok);
  // RESP2 has no native null type -- a null bulk string ("$-1\r\n"), not RESP3's "_\r\n".
  EXPECT_EQ(redis_result.response(), "$-1\r\n");
}

TEST_F(RedisCallForeignTest, ResultBytesForBinaryBulkStringReplyAreExactResp2) {
  std::string result_buffer;
  auto* upstream_callbacks = startCallAndConnect(kCluster, {"GET", "binkey"}, &result_buffer);
  ASSERT_NE(upstream_callbacks, nullptr);

  // Embedded NUL/CR/LF: the length prefix, not a terminator, is what must bound this value on
  // both the wire-in (decode) and wire-out (re-encode) sides.
  std::string binary_payload = std::string("a\0b\r\nc", 6);
  CommonRedis::RespValue value;
  value.type(CommonRedis::RespType::BulkString);
  value.asString() = binary_payload;
  auto reply = encodedReply(value);
  upstream_callbacks->onUpstreamData(reply, false);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  envoy::source::extensions::common::wasm::RedisCallStartResult start_result;
  ASSERT_TRUE(start_result.ParseFromString(result_buffer));
  auto redis_result = deliverViaOnQueueReady(*wasm_, *context_, start_result.queue_token());
  EXPECT_EQ(redis_result.status(), envoy::source::extensions::common::wasm::RedisCallResult::Ok);
  EXPECT_EQ(redis_result.response(), "$6\r\n" + binary_payload + "\r\n");
}

TEST_F(RedisCallForeignTest, ResultBytesForErrorReplyAreExactResp2) {
  std::string result_buffer;
  auto* upstream_callbacks = startCallAndConnect(kCluster, {"GET", "wrongtype"}, &result_buffer);
  ASSERT_NE(upstream_callbacks, nullptr);

  CommonRedis::RespValue value;
  value.type(CommonRedis::RespType::Error);
  value.asString() = "WRONGTYPE Operation against a key holding the wrong kind of value";
  auto reply = encodedReply(value);
  upstream_callbacks->onUpstreamData(reply, false);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  envoy::source::extensions::common::wasm::RedisCallStartResult start_result;
  ASSERT_TRUE(start_result.ParseFromString(result_buffer));
  auto redis_result = deliverViaOnQueueReady(*wasm_, *context_, start_result.queue_token());
  // A Redis-level error reply is still a successfully-completed call from redis_call()'s own
  // perspective (see Context::onRedisCallSuccess) -- RedisCallResult::Ok, not Failure; the error
  // itself is surfaced in the response bytes for the module to interpret.
  EXPECT_EQ(redis_result.status(), envoy::source::extensions::common::wasm::RedisCallResult::Ok);
  EXPECT_EQ(redis_result.response(),
            "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n");
}

TEST_F(RedisCallForeignTest, ResultBytesForNestedArrayReplyAreExactResp2) {
  std::string result_buffer;
  auto* upstream_callbacks = startCallAndConnect(kCluster, {"HGETALL", "myhash"}, &result_buffer);
  ASSERT_NE(upstream_callbacks, nullptr);

  // ["field1", "value1", ["nested1", "nested2"]] -- a flat bulk-string pair followed by a nested
  // array, to exercise recursive re-encoding rather than just a flat list of scalars.
  CommonRedis::RespValue nested;
  nested.type(CommonRedis::RespType::Array);
  std::vector<CommonRedis::RespValue> nested_elements(2);
  nested_elements[0].type(CommonRedis::RespType::BulkString);
  nested_elements[0].asString() = "nested1";
  nested_elements[1].type(CommonRedis::RespType::BulkString);
  nested_elements[1].asString() = "nested2";
  nested.asArray().swap(nested_elements);

  CommonRedis::RespValue value;
  value.type(CommonRedis::RespType::Array);
  std::vector<CommonRedis::RespValue> elements(3);
  elements[0].type(CommonRedis::RespType::BulkString);
  elements[0].asString() = "field1";
  elements[1].type(CommonRedis::RespType::BulkString);
  elements[1].asString() = "value1";
  elements[2] = nested;
  value.asArray().swap(elements);

  auto reply = encodedReply(value);
  upstream_callbacks->onUpstreamData(reply, false);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  envoy::source::extensions::common::wasm::RedisCallStartResult start_result;
  ASSERT_TRUE(start_result.ParseFromString(result_buffer));
  auto redis_result = deliverViaOnQueueReady(*wasm_, *context_, start_result.queue_token());
  EXPECT_EQ(redis_result.status(), envoy::source::extensions::common::wasm::RedisCallResult::Ok);
  EXPECT_EQ(redis_result.response(),
            "*3\r\n$6\r\nfield1\r\n$6\r\nvalue1\r\n*2\r\n$7\r\nnested1\r\n$7\r\nnested2\r\n");
}

TEST_F(RedisCallForeignTest, PoolFailureCompletesSynchronously) {
  std::string result_buffer;
  ASSERT_EQ(callRedis(kCluster, {"GET", "mykey"}, &result_buffer), WasmResult::Ok);

  envoy::source::extensions::common::wasm::RedisCallStartResult start_result;
  ASSERT_TRUE(start_result.ParseFromString(result_buffer));

  thread_local_cluster_.tcp_conn_pool_.poolFailure(
      Tcp::ConnectionPool::PoolFailureReason::RemoteConnectionFailure);

  // onPoolFailure completes synchronously -- no dispatcher post needed.
  auto redis_result = deliverViaOnQueueReady(*wasm_, *context_, start_result.queue_token());
  EXPECT_EQ(redis_result.status(),
            envoy::source::extensions::common::wasm::RedisCallResult::Failure);
  EXPECT_TRUE(redis_result.response().empty());
}

TEST_F(RedisCallForeignTest, ConnectionDropAfterPoolReadyIsDeferredFailure) {
  std::string result_buffer;
  auto* upstream_callbacks = startCallAndConnect(kCluster, {"GET", "mykey"}, &result_buffer);
  ASSERT_NE(upstream_callbacks, nullptr);

  envoy::source::extensions::common::wasm::RedisCallStartResult start_result;
  ASSERT_TRUE(start_result.ParseFromString(result_buffer));

  upstream_callbacks->onEvent(Network::ConnectionEvent::RemoteClose);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  auto redis_result = deliverViaOnQueueReady(*wasm_, *context_, start_result.queue_token());
  EXPECT_EQ(redis_result.status(),
            envoy::source::extensions::common::wasm::RedisCallResult::Failure);
}

TEST_F(RedisCallForeignTest, RootDestroyedAfterDeferredCompletionPostedIsSafe) {
  // RedisCallHandler::onRespValue/deferredFailure post the actual completion to the dispatcher
  // rather than completing inline (see their comments), capturing the root Context* plus its
  // alive_ flag. Destroying the root between that post and the dispatcher running it must not
  // leave the posted lambda holding a dangling pointer.
  std::string result_buffer;
  auto* upstream_callbacks = startCallAndConnect(kCluster, {"GET", "mykey"}, &result_buffer);
  ASSERT_NE(upstream_callbacks, nullptr);

  upstream_callbacks->onEvent(Network::ConnectionEvent::RemoteClose);
  // deferredFailure() has posted the completion to the dispatcher; it has not run yet.

  proxy_wasm::current_context_ = nullptr;
  context_.reset();

  // Running the already-posted completion must be a no-op: ~Context() flips *alive_ to false
  // precisely so the posted lambda checks that instead of dereferencing the now-destroyed root.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

TEST_F(RedisCallForeignTest, ProtocolErrorOnMalformedReplyCompletesSynchronously) {
  std::string result_buffer;
  auto* upstream_callbacks = startCallAndConnect(kCluster, {"GET", "mykey"}, &result_buffer);
  ASSERT_NE(upstream_callbacks, nullptr);

  envoy::source::extensions::common::wasm::RedisCallStartResult start_result;
  ASSERT_TRUE(start_result.ParseFromString(result_buffer));

  // '?' is not a valid RESP2 type marker.
  ::Envoy::Buffer::OwnedImpl malformed("?garbage\r\n");
  // decode() never drains the buffer on a thrown ProtocolError, so the malformed bytes are still
  // sitting unread on the connection; releasing it back to the pool instead of closing it would
  // let a later call acquire it and desync on them. Assert the connection is actually closed, not
  // just that the call fails -- a nulled conn_data_ without this close would still report
  // Failure below.
  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));
  upstream_callbacks->onUpstreamData(malformed, false);

  // The ProtocolError catch branch completes synchronously -- no dispatcher post needed.
  auto redis_result = deliverViaOnQueueReady(*wasm_, *context_, start_result.queue_token());
  EXPECT_EQ(redis_result.status(),
            envoy::source::extensions::common::wasm::RedisCallResult::Failure);
}

TEST_F(RedisCallForeignTest, MalformedBytesAfterValidReplyDeliverFirstResultNotFailure) {
  // Unlike ProtocolErrorOnMalformedReplyCompletesSynchronously above (no valid reply ever
  // decoded), this feeds a complete, valid reply immediately followed, in the same read, by
  // malformed RESP2 bytes. decode() calls onRespValue() for the valid reply first -- which sets
  // completing_ and posts the real completion via dispatcher().post() (see its comment) -- before
  // throwing ProtocolError for the malformed trailer later in the same decode() call. Regression
  // coverage for the bug where onUpstreamData()'s catch block called onRedisCallFailure()
  // unconditionally, racing ahead of the already-posted deferred success, erasing the handler
  // first so finishRedisCall()'s re-entrant-safe find() guard silently turned the posted success
  // into a no-op -- reporting Failure for a call that actually received a valid reply. The
  // connection must still be closed either way: trailing garbage is a real protocol violation and
  // the connection cannot be trusted for reuse.
  std::string result_buffer;
  auto* upstream_callbacks = startCallAndConnect(kCluster, {"GET", "mykey"}, &result_buffer);
  ASSERT_NE(upstream_callbacks, nullptr);

  envoy::source::extensions::common::wasm::RedisCallStartResult start_result;
  ASSERT_TRUE(start_result.ParseFromString(result_buffer));

  auto valid_reply = simpleStringReply("PONG");
  ::Envoy::Buffer::OwnedImpl mixed;
  mixed.add(valid_reply.toString());
  mixed.add("?garbage\r\n"); // '?' is not a valid RESP2 type marker.

  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));
  upstream_callbacks->onUpstreamData(mixed, false);
  // onRespValue defers the real completion via dispatcher().post(); flush it.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  auto redis_result = deliverViaOnQueueReady(*wasm_, *context_, start_result.queue_token());
  EXPECT_EQ(redis_result.status(), envoy::source::extensions::common::wasm::RedisCallResult::Ok);
  EXPECT_THAT(redis_result.response(), testing::HasSubstr("PONG"));
}

TEST_F(RedisCallForeignTest, ClusterNotFound) {
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(absl::string_view("unknown_cluster")))
      .WillOnce(testing::Return(nullptr));

  std::string result_buffer;
  auto result = callRedis("unknown_cluster", {"PING"}, &result_buffer);
  EXPECT_EQ(result, WasmResult::BadArgument);
}

TEST_F(RedisCallForeignTest, NoConnectionPoolAvailable) {
  EXPECT_CALL(thread_local_cluster_, tcpConnPool(testing::_, testing::_))
      .WillOnce(testing::Return(std::nullopt));

  std::string result_buffer;
  auto result = callRedis(kCluster, {"PING"}, &result_buffer);
  EXPECT_EQ(result, WasmResult::BadArgument);
}

TEST_F(RedisCallForeignTest, EmptyCommandArgsIsBadArgument) {
  std::string result_buffer;
  auto result = callRedis(kCluster, {}, &result_buffer);
  EXPECT_EQ(result, WasmResult::BadArgument);
}

TEST_F(RedisCallForeignTest, UnsafeCommandIsRejected) {
  // AUTH/SUBSCRIBE (and the rest of isUnsafeRedisCommand()'s denylist, see context.cc) mutate
  // connection state or produce more than one reply -- unsafe given the connection is released
  // back to the shared pool after exactly one reply. Rejected before a connection is ever
  // acquired, matching clusterNotFound/NoConnectionPoolAvailable's synchronous BadArgument.
  EXPECT_CALL(thread_local_cluster_, tcpConnPool(testing::_, testing::_)).Times(0);

  std::string result_buffer;
  EXPECT_EQ(callRedis(kCluster, {"AUTH", "password"}, &result_buffer), WasmResult::BadArgument);
  // Matching is case-insensitive.
  EXPECT_EQ(callRedis(kCluster, {"subscribe", "channel"}, &result_buffer), WasmResult::BadArgument);
  EXPECT_EQ(callRedis(kCluster, {"MULTI"}, &result_buffer), WasmResult::BadArgument);
  // CLIENT TRACKING/REPLY mutate per-connection state/reply behavior; denylisted wholesale
  // rather than per-subcommand.
  EXPECT_EQ(callRedis(kCluster, {"CLIENT", "TRACKING", "ON"}, &result_buffer),
            WasmResult::BadArgument);
  EXPECT_EQ(callRedis(kCluster, {"CLIENT", "REPLY", "OFF"}, &result_buffer),
            WasmResult::BadArgument);
  // READONLY/READWRITE/ASKING alter per-connection cluster-routing mode.
  EXPECT_EQ(callRedis(kCluster, {"READONLY"}, &result_buffer), WasmResult::BadArgument);
  EXPECT_EQ(callRedis(kCluster, {"READWRITE"}, &result_buffer), WasmResult::BadArgument);
  EXPECT_EQ(callRedis(kCluster, {"ASKING"}, &result_buffer), WasmResult::BadArgument);
  // SYNC/PSYNC/REPLCONF enter replication streaming, producing an unbounded stream of replies.
  EXPECT_EQ(callRedis(kCluster, {"SYNC"}, &result_buffer), WasmResult::BadArgument);
  EXPECT_EQ(callRedis(kCluster, {"PSYNC"}, &result_buffer), WasmResult::BadArgument);
  EXPECT_EQ(callRedis(kCluster, {"REPLCONF"}, &result_buffer), WasmResult::BadArgument);
  // QUIT closes the connection after its single reply, which would otherwise look like any other
  // successful call while releasing a now-dead connection back to the pool as reusable.
  EXPECT_EQ(callRedis(kCluster, {"QUIT"}, &result_buffer), WasmResult::BadArgument);
}

TEST_F(RedisCallForeignTest, MultipleCallsShareOneQueueButHaveDistinctCallIds) {
  std::string result_buffer1;
  ASSERT_EQ(callRedis(kCluster, {"GET", "key1"}, &result_buffer1), WasmResult::Ok);
  std::string result_buffer2;
  ASSERT_EQ(callRedis(kCluster, {"GET", "key2"}, &result_buffer2), WasmResult::Ok);

  envoy::source::extensions::common::wasm::RedisCallStartResult start_result1;
  ASSERT_TRUE(start_result1.ParseFromString(result_buffer1));
  envoy::source::extensions::common::wasm::RedisCallStartResult start_result2;
  ASSERT_TRUE(start_result2.ParseFromString(result_buffer2));

  EXPECT_EQ(start_result1.queue_token(), start_result2.queue_token());
  EXPECT_NE(start_result1.call_id(), start_result2.call_id());

  // Complete both via pool failure (simplest path, doesn't need a connection). MockInstance's
  // handles_/callbacks_ are FIFO (see test/mocks/tcp/mocks.cc), so these two calls complete key1
  // then key2, in the same order they were registered.
  thread_local_cluster_.tcp_conn_pool_.poolFailure(Tcp::ConnectionPool::PoolFailureReason::Timeout);
  thread_local_cluster_.tcp_conn_pool_.poolFailure(Tcp::ConnectionPool::PoolFailureReason::Timeout);

  // Each completion posts its own enqueueSharedQueue + onQueueReady notification; drive both in
  // completion order to confirm delivery order follows completion order.
  auto first_dequeued = deliverViaOnQueueReady(*wasm_, *context_, start_result1.queue_token());
  EXPECT_EQ(first_dequeued.call_id(), start_result1.call_id());

  auto second_dequeued = deliverViaOnQueueReady(*wasm_, *context_, start_result1.queue_token());
  EXPECT_EQ(second_dequeued.call_id(), start_result2.call_id());
}

TEST_F(RedisCallForeignTest, NextCallIdSkipsZeroAndAvoidsCollisionsAcrossWrap) {
  // next_call_id is a wrapping uint32_t counter (see redisCall()'s allocation loop); starting it
  // right at UINT32_MAX exercises both the reserved-zero skip and the collision-avoidance checks
  // against a still-outstanding call_id (tracked in redis_call_request_) and a still-queued,
  // undrained one (tracked in Wasm::RedisCallState::queued_results).
  context_->setNextRedisCallIdForTesting(std::numeric_limits<uint32_t>::max());

  std::string result_buffer1;
  ASSERT_EQ(callRedis(kCluster, {"GET", "key1"}, &result_buffer1), WasmResult::Ok);
  envoy::source::extensions::common::wasm::RedisCallStartResult start_result1;
  ASSERT_TRUE(start_result1.ParseFromString(result_buffer1));
  // The counter's max value is itself a valid id; only the post-wrap 0 is ever skipped.
  EXPECT_EQ(start_result1.call_id(), std::numeric_limits<uint32_t>::max());

  std::string result_buffer2;
  ASSERT_EQ(callRedis(kCluster, {"GET", "key2"}, &result_buffer2), WasmResult::Ok);
  envoy::source::extensions::common::wasm::RedisCallStartResult start_result2;
  ASSERT_TRUE(start_result2.ParseFromString(result_buffer2));
  EXPECT_EQ(start_result2.call_id(), 1u);

  // Completes key1 (FIFO-first outstanding request, see MockInstance::poolFailure) and leaves
  // its result queued, undrained -- moving call_id UINT32_MAX from redis_call_request_ into
  // queued_results rather than freeing it entirely.
  thread_local_cluster_.tcp_conn_pool_.poolFailure(Tcp::ConnectionPool::PoolFailureReason::Timeout);

  // Forcing the counter back to UINT32_MAX must skip it (still in queued_results), skip 0, and
  // skip 1 (key2's call_id, still outstanding) before landing on the first genuinely free id.
  context_->setNextRedisCallIdForTesting(std::numeric_limits<uint32_t>::max());
  std::string result_buffer3;
  ASSERT_EQ(callRedis(kCluster, {"GET", "key3"}, &result_buffer3), WasmResult::Ok);
  envoy::source::extensions::common::wasm::RedisCallStartResult start_result3;
  ASSERT_TRUE(start_result3.ParseFromString(result_buffer3));
  EXPECT_EQ(start_result3.call_id(), 2u);
}

TEST_F(RedisCallForeignTest, CallFromStreamContextUsesRootQueueAndCarriesOriginContextId) {
  auto plugin_handle = std::make_shared<PluginHandle>(std::make_shared<WasmHandle>(wasm_), plugin_);
  auto stream_context = std::make_unique<Context>(wasm_.get(), context_->id(), plugin_handle);

  std::string root_result_buffer;
  ASSERT_EQ(callRedis(kCluster, {"GET", "root_key"}, &root_result_buffer), WasmResult::Ok);

  proxy_wasm::current_context_ = stream_context.get();
  std::string stream_result_buffer;
  ASSERT_EQ(callRedis(kCluster, {"GET", "stream_key"}, &stream_result_buffer), WasmResult::Ok);
  proxy_wasm::current_context_ = context_.get();

  envoy::source::extensions::common::wasm::RedisCallStartResult root_start;
  ASSERT_TRUE(root_start.ParseFromString(root_result_buffer));
  envoy::source::extensions::common::wasm::RedisCallStartResult stream_start;
  ASSERT_TRUE(stream_start.ParseFromString(stream_result_buffer));

  EXPECT_EQ(root_start.queue_token(), stream_start.queue_token());
  EXPECT_NE(root_start.call_id(), stream_start.call_id());

  thread_local_cluster_.tcp_conn_pool_.poolFailure(Tcp::ConnectionPool::PoolFailureReason::Timeout);
  thread_local_cluster_.tcp_conn_pool_.poolFailure(Tcp::ConnectionPool::PoolFailureReason::Timeout);

  // onQueueReady is always delivered on the *root* context (context_), regardless of which
  // Context originated the call -- see redisCall()'s comment on why all state lives on root.
  auto first_dequeued = deliverViaOnQueueReady(*wasm_, *context_, root_start.queue_token());
  EXPECT_EQ(first_dequeued.call_id(), root_start.call_id());
  EXPECT_EQ(first_dequeued.origin_context_id(), context_->id());

  auto second_dequeued = deliverViaOnQueueReady(*wasm_, *context_, root_start.queue_token());
  EXPECT_EQ(second_dequeued.call_id(), stream_start.call_id());
  EXPECT_EQ(second_dequeued.origin_context_id(), stream_context->id());
}

TEST_F(RedisCallForeignTest, TwoWasmInstancesSharingDefaultVmIdDoNotShareQueueOrCallIds) {
  // Constructs a second, fully independent Wasm instance with the same (default, empty) vm_id
  // as the fixture's wasm_ -- NOT via the real Wasm::Wasm(WasmHandleSharedPtr, Dispatcher&)
  // per-worker clone constructor, so this does not exercise an actual worker-thread clone. It
  // still exercises the scenario that matters: same (vm_id, queue_name) is exactly what the
  // process-wide proxy-wasm shared-queue registry keys on, so any two Wasm instances with the
  // same empty vm_id -- cloned or independently constructed -- would collide there if
  // redis_call's queue name weren't also scoped by something unique per Wasm instance.
  testing::NiceMock<Upstream::MockClusterManager> cluster_manager_b;
  testing::NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster_b;
  ON_CALL(cluster_manager_b, getThreadLocalCluster(absl::string_view(kCluster)))
      .WillByDefault(testing::Return(&thread_local_cluster_b));

  envoy::extensions::wasm::v3::PluginConfig plugin_config_b;
  plugin_config_b.mutable_vm_config()->set_runtime("envoy.wasm.runtime.null");
  auto plugin_b = std::make_shared<Plugin>(plugin_config_b, local_info_);
  auto wasm_b = std::make_shared<Wasm>(plugin_b->wasmConfig(), "", scope_, *api_, cluster_manager_b,
                                       *dispatcher_);
  auto context_b = std::make_unique<Context>(wasm_b.get(), plugin_b);

  std::string result_buffer_a;
  ASSERT_EQ(callRedis(kCluster, {"GET", "a"}, &result_buffer_a), WasmResult::Ok);

  proxy_wasm::current_context_ = context_b.get();
  envoy::source::extensions::common::wasm::RedisCallArguments args_b;
  args_b.set_cluster(std::string(kCluster));
  args_b.add_command_args("GET");
  args_b.add_command_args("b");
  args_b.set_timeout_millis(1000);
  std::string serialized_b;
  std::ignore = args_b.SerializeToString(&serialized_b);
  std::string result_buffer_b;
  ASSERT_EQ(proxy_wasm::getForeignFunction("redis_call")(*wasm_b, serialized_b,
                                                         [&result_buffer_b](size_t size) -> void* {
                                                           result_buffer_b.resize(size);
                                                           return result_buffer_b.data();
                                                         }),
            WasmResult::Ok);
  proxy_wasm::current_context_ = context_.get();

  envoy::source::extensions::common::wasm::RedisCallStartResult start_a;
  ASSERT_TRUE(start_a.ParseFromString(result_buffer_a));
  envoy::source::extensions::common::wasm::RedisCallStartResult start_b;
  ASSERT_TRUE(start_b.ParseFromString(result_buffer_b));

  // Both root contexts' Wasm::RedisCallState::next_call_id counters start fresh (distinct Wasm
  // instances get distinct RedisCallState maps), so their first call gets the same call_id --
  // cross-worker disambiguation must come from the queue token itself.
  EXPECT_EQ(start_a.call_id(), start_b.call_id());
  EXPECT_NE(start_a.queue_token(), start_b.queue_token());

  thread_local_cluster_.tcp_conn_pool_.poolFailure(Tcp::ConnectionPool::PoolFailureReason::Timeout);
  thread_local_cluster_b.tcp_conn_pool_.poolFailure(
      Tcp::ConnectionPool::PoolFailureReason::Timeout);

  // Each worker's own Wasm/root routes onQueueReady to its own result, via its own queue token --
  // the exact cross-worker mixup redis_call's wasmInstanceId()-scoped queue name prevents (see
  // Context::redisCall()'s comment on why vm_id/root id alone aren't enough to disambiguate).
  auto result_a = deliverViaOnQueueReady(*wasm_, *context_, start_a.queue_token());
  EXPECT_EQ(result_a.origin_context_id(), context_->id());
  // Worker B's completion did not land on worker A's queue.
  std::string queue_data;
  EXPECT_EQ(context_->dequeueSharedQueue(start_a.queue_token(), &queue_data), WasmResult::Empty);

  auto result_b = deliverViaOnQueueReady(*wasm_b, *context_b, start_b.queue_token());
  EXPECT_EQ(result_b.origin_context_id(), context_b->id());
}

TEST_F(RedisCallForeignTest, ReconfiguredRootReusesQueueInsteadOfLeakingANewOne) {
  // Reconfiguring a plugin (e.g. a changed `configuration` payload) replaces its RootContext
  // object -- and so its numeric id() -- while keeping the same Wasm VM instance (same vm_id) and
  // the same operator-configured root_id. Simulate that here with a second root Context sharing
  // wasm_ and the same (default, empty) root_id as context_, standing in for "the same logical
  // plugin, reconfigured".
  //
  // redisCall()'s queue_name is scoped by root_id(), not by the numeric, per-RootContext-instance
  // root->id() -- so both roots must resolve to the *same* process-wide shared-queue token. Using
  // root->id() instead would register a second, never-cleaned-up entry in that registry on every
  // such reconfiguration (see Context::redisCall()'s comment).
  auto plugin_2 = std::make_shared<Plugin>(plugin_->wasmConfig().config(), local_info_);
  auto second_root = std::make_unique<Context>(wasm_.get(), plugin_2);

  std::string result_buffer1;
  ASSERT_EQ(callRedis(kCluster, {"GET", "key1"}, &result_buffer1), WasmResult::Ok);
  envoy::source::extensions::common::wasm::RedisCallStartResult start_result1;
  ASSERT_TRUE(start_result1.ParseFromString(result_buffer1));

  proxy_wasm::current_context_ = second_root.get();
  std::string result_buffer2;
  ASSERT_EQ(callRedis(kCluster, {"GET", "key2"}, &result_buffer2), WasmResult::Ok);
  proxy_wasm::current_context_ = context_.get();
  envoy::source::extensions::common::wasm::RedisCallStartResult start_result2;
  ASSERT_TRUE(start_result2.ParseFromString(result_buffer2));

  EXPECT_EQ(start_result1.queue_token(), start_result2.queue_token());
}

TEST_F(RedisCallForeignTest, ContextDestroyedWhilePendingCancelsHandle) {
  std::string result_buffer;
  ASSERT_EQ(callRedis(kCluster, {"GET", "mykey"}, &result_buffer), WasmResult::Ok);

  EXPECT_CALL(thread_local_cluster_.tcp_conn_pool_.handles_.front(), cancel(_));

  proxy_wasm::current_context_ = nullptr;
  context_.reset();
}

TEST_F(RedisCallForeignTest, ContextDestroyedAfterPoolReadyClosesConnection) {
  std::string result_buffer;
  startCallAndConnect(kCluster, {"GET", "mykey"}, &result_buffer);

  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));

  proxy_wasm::current_context_ = nullptr;
  context_.reset();
}

TEST_F(RedisCallForeignTest, TimeoutFiresBeforeConnectCancelsHandle) {
  std::string result_buffer;
  ASSERT_EQ(callRedis(kCluster, {"GET", "mykey"}, &result_buffer, /*timeout_millis=*/1000),
            WasmResult::Ok);

  EXPECT_CALL(thread_local_cluster_.tcp_conn_pool_.handles_.front(), cancel(_));

  envoy::source::extensions::common::wasm::RedisCallStartResult start_result;
  ASSERT_TRUE(start_result.ParseFromString(result_buffer));

  std::string queue_data;
  EXPECT_EQ(context_->dequeueSharedQueue(start_result.queue_token(), &queue_data),
            WasmResult::Empty);

  simTime().advanceTimeAndRun(std::chrono::milliseconds(1000), *dispatcher_,
                              Event::Dispatcher::RunType::NonBlock);

  auto redis_result = deliverViaOnQueueReady(*wasm_, *context_, start_result.queue_token());
  EXPECT_EQ(redis_result.status(),
            envoy::source::extensions::common::wasm::RedisCallResult::Failure);
}

TEST_F(RedisCallForeignTest, TimeoutFiresAfterConnectClosesConnection) {
  std::string result_buffer;
  startCallAndConnect(kCluster, {"GET", "mykey"}, &result_buffer, /*timeout_millis=*/1000);

  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));

  simTime().advanceTimeAndRun(std::chrono::milliseconds(1000), *dispatcher_,
                              Event::Dispatcher::RunType::NonBlock);

  envoy::source::extensions::common::wasm::RedisCallStartResult start_result;
  ASSERT_TRUE(start_result.ParseFromString(result_buffer));
  auto redis_result = deliverViaOnQueueReady(*wasm_, *context_, start_result.queue_token());
  EXPECT_EQ(redis_result.status(),
            envoy::source::extensions::common::wasm::RedisCallResult::Failure);
}

TEST_F(RedisCallForeignTest, TimeoutCloseReentrantlyInvokesOnEventButIsSafe) {
  // Network::ConnectionImpl::close() synchronously calls raiseEvent() (see
  // source/common/network/connection_impl.cc), which synchronously invokes every registered
  // Network::ConnectionCallbacks::onEvent() -- including *this* handler's own, since
  // onPoolReady() registered it via addUpstreamCallbacks(). Every other test in this file uses a
  // NiceMock<Network::MockClientConnection> whose close() is a no-op unless an action is
  // configured, so this reentrant call -- onRedisCallTimeout() calling close(), which
  // synchronously re-enters onEvent()/deferredFailure() on the very handler onRedisCallTimeout()
  // is itself in the middle of completing -- is never actually exercised elsewhere. Configure
  // close() here to behave like the real implementation, and confirm the result is still exactly
  // one Failure -- not a crash, and not a double-enqueued result: deferredFailure()'s reentrant
  // completion is only *posted*, so onRedisCallTimeout()'s own (synchronous, direct) call to
  // onRedisCallFailure() below it wins the race and erases the handler first; the reentrant
  // completion later finds nothing and no-ops.
  std::string result_buffer;
  auto* upstream_callbacks =
      startCallAndConnect(kCluster, {"GET", "mykey"}, &result_buffer, /*timeout_millis=*/1000);
  ASSERT_NE(upstream_callbacks, nullptr);

  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush))
      .WillOnce(testing::InvokeWithoutArgs([upstream_callbacks] {
        upstream_callbacks->onEvent(Network::ConnectionEvent::LocalClose);
      }));

  simTime().advanceTimeAndRun(std::chrono::milliseconds(1000), *dispatcher_,
                              Event::Dispatcher::RunType::NonBlock);

  envoy::source::extensions::common::wasm::RedisCallStartResult start_result;
  ASSERT_TRUE(start_result.ParseFromString(result_buffer));
  auto redis_result = deliverViaOnQueueReady(*wasm_, *context_, start_result.queue_token());
  EXPECT_EQ(redis_result.status(),
            envoy::source::extensions::common::wasm::RedisCallResult::Failure);

  // Exactly one result landed on the queue.
  std::string queue_data;
  EXPECT_EQ(context_->dequeueSharedQueue(start_result.queue_token(), &queue_data),
            WasmResult::Empty);
}

TEST_F(RedisCallForeignTest, PoolReadyWriteReentrantCloseIsSafe) {
  // onPoolReady() stores conn_data_ and registers *this* as the connection's UpstreamCallbacks
  // before calling write() -- so a close event raised synchronously out of write() (e.g. a
  // filter on the connection's write path closing it) would reenter onEvent()/deferredFailure()
  // on *this* handler while onPoolReady() is still on the call stack, before it returns. This is
  // safe for the same reason as TimeoutCloseReentrantlyInvokesOnEventButIsSafe above: onEvent()
  // already no-ops once conn_data_ is null (which deferredFailure() itself sets before posting,
  // so a second reentrant call can't post twice), and onPoolReady() touches no fields after
  // calling write(), so there's nothing left for it to do once write() returns. Prove it rather
  // than merely assert it, since every other test's write() is a no-op mock call.
  std::string result_buffer;
  ASSERT_EQ(callRedis(kCluster, {"GET", "mykey"}, &result_buffer), WasmResult::Ok);

  Tcp::ConnectionPool::UpstreamCallbacks* captured = nullptr;
  EXPECT_CALL(*thread_local_cluster_.tcp_conn_pool_.connection_data_, addUpstreamCallbacks(_))
      .WillOnce([&captured](Tcp::ConnectionPool::UpstreamCallbacks& cb) { captured = &cb; });
  EXPECT_CALL(connection_, write(_, false)).WillOnce(testing::InvokeWithoutArgs([&captured] {
    captured->onEvent(Network::ConnectionEvent::RemoteClose);
  }));
  thread_local_cluster_.tcp_conn_pool_.poolReady(connection_);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  envoy::source::extensions::common::wasm::RedisCallStartResult start_result;
  ASSERT_TRUE(start_result.ParseFromString(result_buffer));
  auto redis_result = deliverViaOnQueueReady(*wasm_, *context_, start_result.queue_token());
  EXPECT_EQ(redis_result.status(),
            envoy::source::extensions::common::wasm::RedisCallResult::Failure);

  // Exactly one result landed on the queue -- not a double-enqueue from two posted completions.
  std::string queue_data;
  EXPECT_EQ(context_->dequeueSharedQueue(start_result.queue_token(), &queue_data),
            WasmResult::Empty);
}

TEST_F(RedisCallForeignTest, TimeoutDoesNotFireAfterEarlyCompletion) {
  std::string result_buffer;
  ASSERT_EQ(callRedis(kCluster, {"GET", "mykey"}, &result_buffer, /*timeout_millis=*/1000),
            WasmResult::Ok);

  // Completes well before the 1000ms timeout; the handler (and its timer) is destroyed as part
  // of this, so the timer must not fire later and double-deliver a result.
  thread_local_cluster_.tcp_conn_pool_.poolFailure(
      Tcp::ConnectionPool::PoolFailureReason::RemoteConnectionFailure);

  envoy::source::extensions::common::wasm::RedisCallStartResult start_result;
  ASSERT_TRUE(start_result.ParseFromString(result_buffer));
  deliverViaOnQueueReady(*wasm_, *context_, start_result.queue_token());

  simTime().advanceTimeAndRun(std::chrono::milliseconds(1000), *dispatcher_,
                              Event::Dispatcher::RunType::NonBlock);

  // Nothing new was enqueued by a (nonexistent) timeout firing: the queue is empty again.
  std::string queue_data;
  EXPECT_EQ(context_->dequeueSharedQueue(start_result.queue_token(), &queue_data),
            WasmResult::Empty);
}

TEST_F(RedisCallForeignTest, TimerCannotFireBetweenDeferredSuccessPostAndItRunning) {
  // Unlike TimeoutDoesNotFireAfterEarlyCompletion above (where completion -- and so handler/timer
  // destruction -- happens synchronously), onRespValue only *posts* the real completion (see its
  // comment); there would otherwise be a window, between the reply being fully decoded and that
  // posted lambda actually running, where the handler is still present in redis_call_request_
  // with conn_data_ already null. If the timer fired in that window, onRedisCallTimeout would see
  // conn_data_ == nullptr and cancel_handle_ == nullptr, fall through to onRedisCallFailure, and
  // erase the handler with a bogus Failure before the real, already-decoded success ever gets
  // delivered -- so the real posted success lambda would later find nothing to deliver, silently
  // dropping the genuine reply. onRespValue disables the timer synchronously, before posting,
  // specifically to close this window regardless of how the real event loop would have ordered
  // the two. Simulate the worst case directly: fire the timeout in exactly that window, via the
  // test-only onRedisCallTimeoutForTesting() seam, before the posted completion runs.
  std::string result_buffer;
  auto* upstream_callbacks =
      startCallAndConnect(kCluster, {"GET", "mykey"}, &result_buffer, /*timeout_millis=*/1000);
  ASSERT_NE(upstream_callbacks, nullptr);

  envoy::source::extensions::common::wasm::RedisCallStartResult start_result;
  ASSERT_TRUE(start_result.ParseFromString(result_buffer));

  auto reply = simpleStringReply("PONG");
  upstream_callbacks->onUpstreamData(reply, false); // Decodes synchronously; posts completion.

  // The window: conn_data_ is already null, but the posted success completion hasn't run yet.
  context_->onRedisCallTimeoutForTesting(start_result.call_id());

  // Flush the posted completion.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  auto redis_result = deliverViaOnQueueReady(*wasm_, *context_, start_result.queue_token());
  EXPECT_EQ(redis_result.status(), envoy::source::extensions::common::wasm::RedisCallResult::Ok);
  EXPECT_THAT(redis_result.response(), testing::HasSubstr("PONG"));

  // Only one result was ever enqueued -- not a real success preceded by a bogus timeout failure.
  std::string queue_data;
  EXPECT_EQ(context_->dequeueSharedQueue(start_result.queue_token(), &queue_data),
            WasmResult::Empty);
}

TEST_F(RedisCallForeignTest, InvalidTimeoutIsRejected) {
  std::string result_buffer;
  // 0 is "no timeout", which redisCall() rejects -- see Context::redisCall(); it is not treated
  // as "wait forever".
  EXPECT_EQ(callRedis(kCluster, {"PING"}, &result_buffer, /*timeout_millis=*/0),
            WasmResult::BadArgument);
  // Over context.cc's kMaxRedisCallTimeoutMillis (5 minutes).
  EXPECT_EQ(callRedis(kCluster, {"PING"}, &result_buffer, /*timeout_millis=*/300001),
            WasmResult::BadArgument);
  EXPECT_EQ(callRedis(kCluster, {"PING"}, &result_buffer,
                      /*timeout_millis=*/std::numeric_limits<uint32_t>::max()),
            WasmResult::BadArgument);
}

TEST_F(RedisCallForeignTest, OutstandingCallsAreNotChargedAgainstTheByteBudget) {
  // An outstanding (not yet completed) call holds no reserved buffer, so admission doesn't charge
  // it anything -- unlike a count-based limit, a tiny byte budget doesn't cap how many calls can
  // be outstanding at once; that's left to the target cluster's own circuit breakers.
  context_->setMaxQueuedRedisCallBytesForTesting(1);

  std::string result_buffer1;
  std::string result_buffer2;
  ASSERT_EQ(callRedis(kCluster, {"GET", "key1"}, &result_buffer1), WasmResult::Ok);
  EXPECT_EQ(callRedis(kCluster, {"GET", "key2"}, &result_buffer2), WasmResult::Ok);
}

TEST_F(RedisCallForeignTest, DequeueDoesNotCreditAccountingForModuleInjectedData) {
  // dequeueSharedQueue()'s admission accounting must only release queued_bytes for a genuine,
  // host-produced result (see Wasm::RedisCallState::queued_results' comment) -- not for any
  // successful dequeue on the token. proxy-wasm shared queues are guest-writable: a module holds
  // the same raw queue_token redis_call hands back and can call
  // proxy_enqueue_shared_queue/proxy_dequeue_shared_queue on it directly, not just via redis_call's
  // intended round trip. Regression coverage for the bug where any successful dequeue, regardless
  // of what was dequeued, released budget -- letting a module inject-and-drain its own data to
  // admit more calls than the documented byte budget allows while a real result is still
  // undrained.
  context_->setMaxQueuedRedisCallBytesForTesting(1);

  std::string result_buffer1;
  ASSERT_EQ(callRedis(kCluster, {"GET", "key1"}, &result_buffer1), WasmResult::Ok);
  envoy::source::extensions::common::wasm::RedisCallStartResult start_result1;
  ASSERT_TRUE(start_result1.ParseFromString(result_buffer1));

  // Forge both pieces of module-injected data onto the queue *before* completing call1, so they
  // sit ahead of its real result in FIFO order and get drained first below -- otherwise the real
  // result, enqueued by the completion below, would simply come out ahead of them instead.
  ASSERT_EQ(context_->enqueueSharedQueue(start_result1.queue_token(), "not a redis call result"),
            WasmResult::Ok);
  // A well-formed RedisCallResult, but with a call_id this plugin instance never actually issued
  // -- not tracked in queued_results -- must be rejected the same way as unparseable garbage.
  envoy::source::extensions::common::wasm::RedisCallResult forged_result;
  forged_result.set_status(envoy::source::extensions::common::wasm::RedisCallResult::Ok);
  forged_result.set_call_id(999999);
  std::string forged_serialized;
  ASSERT_TRUE(forged_result.SerializeToString(&forged_serialized));
  ASSERT_EQ(context_->enqueueSharedQueue(start_result1.queue_token(), forged_serialized),
            WasmResult::Ok);

  // Completes synchronously (see PoolFailureCompletesSynchronously) and leaves one undrained
  // result -- a few bytes, but enough to exceed the 1-byte budget above -- on the shared queue,
  // behind the two forged entries above.
  thread_local_cluster_.tcp_conn_pool_.poolFailure(Tcp::ConnectionPool::PoolFailureReason::Timeout);

  std::string result_buffer2;
  EXPECT_EQ(callRedis(kCluster, {"GET", "key2"}, &result_buffer2), WasmResult::InternalFailure);

  // Draining the unparseable garbage first must not free call1's real budget.
  std::string dequeued;
  EXPECT_EQ(context_->dequeueSharedQueue(start_result1.queue_token(), &dequeued), WasmResult::Ok);
  EXPECT_EQ(dequeued, "not a redis call result");
  EXPECT_EQ(callRedis(kCluster, {"GET", "key3"}, &result_buffer2), WasmResult::InternalFailure);

  // Nor must draining the well-formed-but-untracked forged result.
  EXPECT_EQ(context_->dequeueSharedQueue(start_result1.queue_token(), &dequeued), WasmResult::Ok);
  EXPECT_EQ(dequeued, forged_serialized);
  EXPECT_EQ(callRedis(kCluster, {"GET", "key4"}, &result_buffer2), WasmResult::InternalFailure);

  // Draining the real call1 result is what actually frees its budget.
  EXPECT_EQ(context_->dequeueSharedQueue(start_result1.queue_token(), &dequeued), WasmResult::Ok);
  std::string result_buffer5;
  EXPECT_EQ(callRedis(kCluster, {"GET", "key5"}, &result_buffer5), WasmResult::Ok);
}

TEST_F(RedisCallForeignTest, OversizedReplyIsReportedAsFailure) {
  // kMaxRedisResponseBytes (context.cc, 10 MiB) caps the RESP2-re-encoded reply size enqueued per
  // result -- a reply over that cap must not be queued as Ok with a giant payload.
  std::string result_buffer;
  auto* upstream_callbacks = startCallAndConnect(kCluster, {"GET", "mykey"}, &result_buffer);
  ASSERT_NE(upstream_callbacks, nullptr);

  envoy::source::extensions::common::wasm::RedisCallStartResult start_result;
  ASSERT_TRUE(start_result.ParseFromString(result_buffer));

  CommonRedis::RespValue value;
  value.type(CommonRedis::RespType::BulkString);
  value.asString() = std::string(10 * 1024 * 1024 + 1, 'a'); // Just over the cap.
  ::Envoy::Buffer::OwnedImpl reply;
  CommonRedis::EncoderImpl().encode(value, reply);
  upstream_callbacks->onUpstreamData(reply, false);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  auto redis_result = deliverViaOnQueueReady(*wasm_, *context_, start_result.queue_token());
  EXPECT_EQ(redis_result.status(),
            envoy::source::extensions::common::wasm::RedisCallResult::Failure);
  EXPECT_TRUE(redis_result.response().empty());
}

TEST_F(RedisCallForeignTest, FragmentedOversizedReplyFailsBeforeExceedingCap) {
  // Unlike OversizedReplyIsReportedAsFailure above (one read, already over the cap once fully
  // decoded), this fragments the same kind of oversized reply across many reads that are each
  // individually well under kMaxRedisResponseBytes (10 MiB) -- regression coverage for
  // onUpstreamData() cumulatively tracking received_bytes_ across calls, rather than only
  // checking the current read's length. DecoderImpl buffers an in-progress value's bytes across
  // decode() calls (see its own "always consume all bytes" contract), so without that cumulative
  // tracking this reply would keep growing, unrejected, up to the codec's own much larger
  // kMaxBulkStringLength (512 MiB).
  std::string result_buffer;
  auto* upstream_callbacks = startCallAndConnect(kCluster, {"GET", "bigkey"}, &result_buffer);
  ASSERT_NE(upstream_callbacks, nullptr);

  envoy::source::extensions::common::wasm::RedisCallStartResult start_result;
  ASSERT_TRUE(start_result.ParseFromString(result_buffer));

  // Announce a reply far larger than the 10 MiB cap, then feed its body in 1 MiB chunks -- never
  // large enough on their own to trip a check that only looked at a single read's length.
  ::Envoy::Buffer::OwnedImpl header;
  header.add("$20000000\r\n");
  upstream_callbacks->onUpstreamData(header, false);

  constexpr size_t kChunkBytes = 1024 * 1024; // 1 MiB
  const std::string chunk_data(kChunkBytes, 'a');
  // 9 chunks (9 MiB) plus the 11-byte header stay under the 10 MiB cap; the 10th is what must
  // push the cumulative total over it.
  for (int i = 0; i < 9; ++i) {
    ::Envoy::Buffer::OwnedImpl body;
    body.add(chunk_data);
    upstream_callbacks->onUpstreamData(body, false);
  }

  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));
  ::Envoy::Buffer::OwnedImpl final_chunk;
  final_chunk.add(chunk_data);
  upstream_callbacks->onUpstreamData(final_chunk, false);
  // The oversized-cumulative-read check calls onRedisCallFailure (and so enqueueSharedQueue)
  // synchronously, so only the enqueueSharedQueue call_on_thread post needs flushing.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  auto redis_result = deliverViaOnQueueReady(*wasm_, *context_, start_result.queue_token());
  EXPECT_EQ(redis_result.status(),
            envoy::source::extensions::common::wasm::RedisCallResult::Failure);
  EXPECT_TRUE(redis_result.response().empty());
}

TEST_F(RedisCallForeignTest, StreamContextDestructionDoesNotCancelPendingCall) {
  // Destroying the Context that issued redisCall() does not cancel it: per-call state always
  // lives on rootContext() (see redisCall()), and proxy-wasm has no API to cancel a specific
  // queue entry -- timeout_millis is the only bound on how long it can outlive its originator.
  auto plugin_handle = std::make_shared<PluginHandle>(std::make_shared<WasmHandle>(wasm_), plugin_);
  auto stream_context = std::make_unique<Context>(wasm_.get(), context_->id(), plugin_handle);
  uint32_t origin_context_id = stream_context->id();

  proxy_wasm::current_context_ = stream_context.get();
  std::string result_buffer;
  ASSERT_EQ(callRedis(kCluster, {"GET", "mykey"}, &result_buffer), WasmResult::Ok);
  proxy_wasm::current_context_ = context_.get();

  envoy::source::extensions::common::wasm::RedisCallStartResult start_result;
  ASSERT_TRUE(start_result.ParseFromString(result_buffer));

  stream_context.reset();

  thread_local_cluster_.tcp_conn_pool_.poolFailure(Tcp::ConnectionPool::PoolFailureReason::Timeout);

  auto redis_result = deliverViaOnQueueReady(*wasm_, *context_, start_result.queue_token());
  EXPECT_EQ(redis_result.status(),
            envoy::source::extensions::common::wasm::RedisCallResult::Failure);
  EXPECT_EQ(redis_result.origin_context_id(), origin_context_id);
}

TEST_F(RedisCallForeignTest, SecondReplyInSameReadClosesConnectionButDeliversFirstResult) {
  // Two complete RESP2 replies arriving back-to-back in a single read: DecoderImpl::decode()
  // parses both and invokes RedisCallHandler::onRespValue() twice within this one
  // onUpstreamData() call (see its comment). A single command must have exactly one reply, so
  // the peer sending a second, unsolicited one means the connection can no longer be trusted for
  // reuse -- it must be closed rather than released back to the pool as healthy -- while the
  // legitimate first result is still delivered exactly once.
  std::string result_buffer;
  auto* upstream_callbacks = startCallAndConnect(kCluster, {"GET", "mykey"}, &result_buffer);
  ASSERT_NE(upstream_callbacks, nullptr);

  envoy::source::extensions::common::wasm::RedisCallStartResult start_result;
  ASSERT_TRUE(start_result.ParseFromString(result_buffer));

  auto first_reply = simpleStringReply("PONG");
  auto second_reply = simpleStringReply("UNSOLICITED");
  ::Envoy::Buffer::OwnedImpl two_replies;
  two_replies.add(first_reply.toString());
  two_replies.add(second_reply.toString());

  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));
  upstream_callbacks->onUpstreamData(two_replies, false);
  // onRespValue defers completion via dispatcher().post(); flush it.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  auto redis_result = deliverViaOnQueueReady(*wasm_, *context_, start_result.queue_token());
  EXPECT_EQ(redis_result.status(), envoy::source::extensions::common::wasm::RedisCallResult::Ok);
  EXPECT_THAT(redis_result.response(), testing::HasSubstr("PONG"));

  // The second, unsolicited value was never enqueued -- not silently delivered later as if it
  // were some other call's result.
  std::string queue_data;
  EXPECT_EQ(context_->dequeueSharedQueue(start_result.queue_token(), &queue_data),
            WasmResult::Empty);
}

TEST_F(RedisCallForeignTest, ClosesConnectionWhenSecondReplyIsOnlyPartial) {
  // Unlike SecondReplyInSameReadClosesConnectionButDeliversFirstResult above (second reply fully
  // decoded), this leaves the second, unsolicited reply incomplete -- decode() consumes its bytes
  // into the decoder's internal buffering state without ever calling onRespValue() a second time,
  // so completing_ and conn_data_ alone look identical to a genuinely clean single-reply read.
  // Regression coverage for onUpstreamData() checking decoder_->hasBufferedData() before
  // releasing the connection back to the pool as healthy.
  std::string result_buffer;
  auto* upstream_callbacks = startCallAndConnect(kCluster, {"GET", "mykey"}, &result_buffer);
  ASSERT_NE(upstream_callbacks, nullptr);

  envoy::source::extensions::common::wasm::RedisCallStartResult start_result;
  ASSERT_TRUE(start_result.ParseFromString(result_buffer));

  // A complete reply ("+PONG\r\n") immediately followed, in the same read, by the start of a
  // second, unsolicited reply that never finishes ("$5\r\nHel" -- a 5-byte bulk string with only
  // 3 body bytes delivered).
  auto reply = simpleStringReply("PONG");
  reply.add("$5\r\nHel");

  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));
  upstream_callbacks->onUpstreamData(reply, false);
  // onRespValue defers completion via dispatcher().post(); flush it.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  auto redis_result = deliverViaOnQueueReady(*wasm_, *context_, start_result.queue_token());
  EXPECT_EQ(redis_result.status(), envoy::source::extensions::common::wasm::RedisCallResult::Ok);
  EXPECT_THAT(redis_result.response(), testing::HasSubstr("PONG"));
}

TEST_F(RedisCallForeignTest, QueuedByteBudgetRejectsNewCallsUntilReservationIsReleased) {
  // kMaxQueuedRedisCallBytes is production-sized (100 MiB); lower it so the test doesn't need a
  // multi-megabyte payload to exercise the bound. Nothing is charged at admission (see
  // redisCall()'s comment), so both calls below are admitted freely while outstanding; only once
  // the first one's real, tiny serialized result actually lands on the shared queue does it
  // exceed this tiny budget and start rejecting new calls.
  context_->setMaxQueuedRedisCallBytesForTesting(1);

  std::string result_buffer1;
  ASSERT_EQ(callRedis(kCluster, {"GET", "key1"}, &result_buffer1), WasmResult::Ok);

  std::string result_buffer2;
  // Still admitted: call1 is outstanding, not completed, so it hasn't charged anything yet.
  ASSERT_EQ(callRedis(kCluster, {"GET", "key2"}, &result_buffer2), WasmResult::Ok);

  // Completing call1 charges its real (small, but over the 1-byte budget) serialized size.
  thread_local_cluster_.tcp_conn_pool_.poolFailure(Tcp::ConnectionPool::PoolFailureReason::Timeout);

  std::string result_buffer3;
  EXPECT_EQ(callRedis(kCluster, {"GET", "key3"}, &result_buffer3), WasmResult::InternalFailure);

  // Draining call1's result releases its charge, freeing budget for a new call.
  envoy::source::extensions::common::wasm::RedisCallStartResult start_result1;
  ASSERT_TRUE(start_result1.ParseFromString(result_buffer1));
  std::string queue_data;
  EXPECT_EQ(context_->dequeueSharedQueue(start_result1.queue_token(), &queue_data), WasmResult::Ok);

  EXPECT_EQ(callRedis(kCluster, {"GET", "key4"}, &result_buffer3), WasmResult::Ok);
}

TEST_F(RedisCallForeignTest, ReconfiguredRootPreservesByteBudgetAndCallIdSequence) {
  // Wasm::RedisCallState (byte accounting, next_call_id) is keyed by root_id() and lives on the
  // Wasm VM specifically so it survives RootContext replacement on reconfiguration (see its
  // comment). Unlike ReconfiguredRootReusesQueueInsteadOfLeakingANewOne above, which only asserts
  // the queue *token* is reused, this asserts the accounting itself carries over: a result queued
  // by the old root still counts against the budget under the new root, and call_ids keep
  // incrementing rather than restarting at 1 and colliding with an old, still-undrained result's
  // call_id.
  auto plugin_2 = std::make_shared<Plugin>(plugin_->wasmConfig().config(), local_info_);
  auto second_root = std::make_unique<Context>(wasm_.get(), plugin_2);
  // Both roots' own copies of this testing override must be lowered: whichever root happens to
  // issue a given call is the one redisCall() reads it from.
  context_->setMaxQueuedRedisCallBytesForTesting(1);
  second_root->setMaxQueuedRedisCallBytesForTesting(1);

  std::string result_buffer1;
  ASSERT_EQ(callRedis(kCluster, {"GET", "key1"}, &result_buffer1), WasmResult::Ok);
  envoy::source::extensions::common::wasm::RedisCallStartResult start_result1;
  ASSERT_TRUE(start_result1.ParseFromString(result_buffer1));

  // Completes synchronously (see PoolFailureCompletesSynchronously) and leaves one undrained
  // result on the shared queue.
  thread_local_cluster_.tcp_conn_pool_.poolFailure(Tcp::ConnectionPool::PoolFailureReason::Timeout);

  // The replacement root inherits the exhausted budget from the old root's still-undrained
  // result -- not a fresh depth of 0 -- even though its own redis_call_request_ map is empty.
  proxy_wasm::current_context_ = second_root.get();
  std::string result_buffer2;
  EXPECT_EQ(callRedis(kCluster, {"GET", "key2"}, &result_buffer2), WasmResult::InternalFailure);

  // Draining the old result frees the slot for the new root.
  std::string queue_data;
  EXPECT_EQ(second_root->dequeueSharedQueue(start_result1.queue_token(), &queue_data),
            WasmResult::Ok);
  EXPECT_EQ(callRedis(kCluster, {"GET", "key2"}, &result_buffer2), WasmResult::Ok);
  proxy_wasm::current_context_ = context_.get();

  envoy::source::extensions::common::wasm::RedisCallStartResult start_result2;
  ASSERT_TRUE(start_result2.ParseFromString(result_buffer2));

  // call_id kept incrementing across the replacement rather than restarting at 1 -- which would
  // otherwise collide with the already-call_id-1 result the old root issued.
  EXPECT_NE(start_result1.call_id(), start_result2.call_id());
  EXPECT_EQ(start_result1.queue_token(), start_result2.queue_token());
}

// Two distinct plugin instances sharing a VM and the same (default, empty) root_id must not
// resolve to the same RedisCallState/shared queue just because plugin_key() omits the name --
// that would let one plugin's call_id/queue accounting clobber the other's.
TEST_F(RedisCallForeignTest, DistinctPluginNamesSharingVmAndRootIdDoNotCollide) {
  envoy::extensions::wasm::v3::PluginConfig plugin_config_2;
  plugin_config_2.set_name("other_plugin");
  plugin_config_2.mutable_vm_config()->set_runtime("envoy.wasm.runtime.null");
  auto plugin_2 = std::make_shared<Plugin>(plugin_config_2, local_info_);
  auto second_root = std::make_unique<Context>(wasm_.get(), plugin_2);

  std::string result_buffer1;
  ASSERT_EQ(callRedis(kCluster, {"GET", "key1"}, &result_buffer1), WasmResult::Ok);
  envoy::source::extensions::common::wasm::RedisCallStartResult start_result1;
  ASSERT_TRUE(start_result1.ParseFromString(result_buffer1));

  proxy_wasm::current_context_ = second_root.get();
  std::string result_buffer2;
  ASSERT_EQ(callRedis(kCluster, {"GET", "key2"}, &result_buffer2), WasmResult::Ok);
  proxy_wasm::current_context_ = context_.get();
  envoy::source::extensions::common::wasm::RedisCallStartResult start_result2;
  ASSERT_TRUE(start_result2.ParseFromString(result_buffer2));

  // Distinct plugin instances get distinct shared-queue registrations -- not the collision this
  // test guards against.
  EXPECT_NE(start_result1.queue_token(), start_result2.queue_token());
  // Each plugin's RedisCallState::next_call_id is its own independent counter starting at 1, not
  // one shared sequence -- both calls being each instance's first, both get call_id 1.
  EXPECT_EQ(start_result1.call_id(), start_result2.call_id());

  thread_local_cluster_.tcp_conn_pool_.poolFailure(Tcp::ConnectionPool::PoolFailureReason::Timeout);
  thread_local_cluster_.tcp_conn_pool_.poolFailure(Tcp::ConnectionPool::PoolFailureReason::Timeout);

  auto result_1 = deliverViaOnQueueReady(*wasm_, *context_, start_result1.queue_token());
  EXPECT_EQ(result_1.call_id(), start_result1.call_id());

  proxy_wasm::current_context_ = second_root.get();
  auto result_2 = deliverViaOnQueueReady(*wasm_, *second_root, start_result2.queue_token());
  EXPECT_EQ(result_2.call_id(), start_result2.call_id());
  proxy_wasm::current_context_ = context_.get();
}

// During reconfiguration, an old configuration generation's RootContext can stay alive
// (in-flight streams still reference it) concurrently with the new generation's, since
// getOrCreateThreadLocalPlugin's local_plugins cache is a weak_ptr cache. Two PluginConfigs with
// the same name/root_id but different `configuration` model exactly that: without `configuration`
// folded into plugin()->key(), both generations would resolve to the same RedisCallState/queue,
// so a call from either could retarget delivery for both.
TEST_F(RedisCallForeignTest, DistinctConfigurationGenerationsSharingNameAndRootIdDoNotCollide) {
  auto plugin_config_2 = plugin_->wasmConfig().config();
  plugin_config_2.mutable_configuration()->set_value("generation-2-config");
  auto plugin_2 = std::make_shared<Plugin>(plugin_config_2, local_info_);
  auto second_root = std::make_unique<Context>(wasm_.get(), plugin_2);

  // Old generation (context_) issues a call first.
  std::string result_buffer1;
  ASSERT_EQ(callRedis(kCluster, {"GET", "key1"}, &result_buffer1), WasmResult::Ok);
  envoy::source::extensions::common::wasm::RedisCallStartResult start_result1;
  ASSERT_TRUE(start_result1.ParseFromString(result_buffer1));

  // New generation (second_root) issues its own call while the old generation's root is still
  // alive -- the concurrently-live-generations scenario the existing reconfiguration tests don't
  // cover.
  proxy_wasm::current_context_ = second_root.get();
  std::string result_buffer2;
  ASSERT_EQ(callRedis(kCluster, {"GET", "key2"}, &result_buffer2), WasmResult::Ok);
  proxy_wasm::current_context_ = context_.get();
  envoy::source::extensions::common::wasm::RedisCallStartResult start_result2;
  ASSERT_TRUE(start_result2.ParseFromString(result_buffer2));

  // Each generation gets its own shared-queue dispatch target -- the new generation's redisCall()
  // must never retarget (or share admission accounting with) the old generation's.
  EXPECT_NE(start_result1.queue_token(), start_result2.queue_token());
  EXPECT_EQ(start_result1.call_id(), start_result2.call_id());

  thread_local_cluster_.tcp_conn_pool_.poolFailure(Tcp::ConnectionPool::PoolFailureReason::Timeout);
  thread_local_cluster_.tcp_conn_pool_.poolFailure(Tcp::ConnectionPool::PoolFailureReason::Timeout);

  // The old generation's result is only ever delivered to the old generation, never the new one.
  auto result_1 = deliverViaOnQueueReady(*wasm_, *context_, start_result1.queue_token());
  EXPECT_EQ(result_1.call_id(), start_result1.call_id());

  proxy_wasm::current_context_ = second_root.get();
  auto result_2 = deliverViaOnQueueReady(*wasm_, *second_root, start_result2.queue_token());
  EXPECT_EQ(result_2.call_id(), start_result2.call_id());
  proxy_wasm::current_context_ = context_.get();
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
