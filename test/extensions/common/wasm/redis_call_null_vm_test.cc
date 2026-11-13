#include "envoy/server/lifecycle_notifier.h"
#include "envoy/tcp/conn_pool.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/common/wasm/ext/redis_call.pb.h"
#include "source/extensions/common/wasm/wasm.h"
#include "source/extensions/filters/network/common/redis/codec_impl.h"

#include "test/mocks/init/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/connection.h"
#include "test/mocks/server/server_lifecycle_notifier.h"
#include "test/mocks/tcp/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"
#include "test/test_common/wasm_base.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

namespace CommonRedis = ::Envoy::Extensions::NetworkFilters::Common::Redis;

using testing::_;

// Unlike RedisCallForeignTest, which drives ContextBase::onQueueReady() directly on a host-side
// Context (see that fixture's deliverViaOnQueueReady() comment), this fixture loads a real Null VM
// module (RedisCallNullPlugin, test_data/test_redis_call_cpp.cc) via the production
// createWasm()/getOrCreateThreadLocalPlugin() path, and asserts on log output the module only
// produces after decoding a RedisCallResult delivered through the real
// SharedQueue::enqueue -> getThreadLocalWasm -> onQueueReady -> proxy_on_queue_ready chain.
class RedisCallNullVmTest : public testing::Test, public Event::TestUsingSimulatedTime {
public:
  static constexpr absl::string_view kCluster = "redis_cluster";

  // Intercepts the module's logWarn() calls, which for the Null VM runtime resolve directly to
  // Context::log() rather than a real ABI call -- same technique as wasm_test.cc's TestContext.
  class TestContext : public Context {
  public:
    using Context::Context;
    MOCK_CONTEXT_LOG_;
  };

  RedisCallNullVmTest()
      : api_(Api::createApiForTest(stats_store_, simTime())),
        dispatcher_(api_->allocateDispatcher("redis_call_null_vm_test")),
        scope_(stats_store_.createScope("")) {
    envoy::extensions::wasm::v3::PluginConfig plugin_config;
    plugin_config.mutable_vm_config()->set_vm_id("redis_call_null_vm_test");
    plugin_config.mutable_vm_config()->set_runtime("envoy.wasm.runtime.null");
    plugin_config.mutable_vm_config()->mutable_code()->mutable_local()->set_inline_bytes(
        "RedisCallNullPlugin");
    plugin_ = std::make_shared<Plugin>(plugin_config, local_info_);

    CreateContextFn create_root = [this](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) {
      root_context_ = new TestContext(wasm, plugin);
      ++root_contexts_created_;
      return root_context_;
    };
    createWasm(
        plugin_, scope_, cluster_manager_, init_manager_, *dispatcher_, *api_, lifecycle_notifier_,
        remote_data_provider_, [this](WasmHandleSharedPtr wasm) { wasm_ = wasm; }, create_root);
    plugin_handle_ = getOrCreateThreadLocalPlugin(wasm_, plugin_, *dispatcher_, create_root);
    wasm_ = plugin_handle_->wasmHandle();
    proxy_wasm::current_context_ = root_context_;

    ON_CALL(cluster_manager_, getThreadLocalCluster(absl::string_view(kCluster)))
        .WillByDefault(testing::Return(&thread_local_cluster_));
  }

  ~RedisCallNullVmTest() override { proxy_wasm::current_context_ = nullptr; }

  // Serializes RedisCallArguments and invokes the real "redis_call" foreign function, mirroring
  // what the loaded module does via proxy_call_foreign_function.
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
    return function(*wasm_->wasm(), serialized, [result_buffer](size_t size) -> void* {
      result_buffer->resize(size);
      return result_buffer->data();
    });
  }

  static ::Envoy::Buffer::OwnedImpl simpleStringReply(absl::string_view s) {
    CommonRedis::RespValue value;
    value.type(CommonRedis::RespType::SimpleString);
    value.asString() = std::string(s);
    ::Envoy::Buffer::OwnedImpl buffer;
    CommonRedis::EncoderImpl().encode(value, buffer);
    return buffer;
  }

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

  // Destroys root_context_ the way production reconfiguration does -- not by dropping a
  // unique_ptr in a test, but via PluginHandleBase::~PluginHandleBase() -> WasmBase::
  // startShutdown(), which is what actually runs ~Context() on the old root -- then re-derives a
  // plugin handle for the same plugin key. Since the old entry was erased from root_contexts_ by
  // startShutdown(), WasmBase::start() creates a genuinely new root context via create_root rather
  // than returning the destroyed one, the same as getOrCreateThreadLocalPlugin() does in
  // production when a worker thread picks up a new configuration. Updates root_context_,
  // plugin_handle_, wasm_, and proxy_wasm::current_context_ to the new instance.
  //
  // Asserts via root_contexts_created_ (not pointer inequality against the old root_context_)
  // that create_root really ran again: the old TestContext is freed before the new one is
  // allocated, so the allocator is free to (and in practice does) hand back the exact same
  // address for the new object -- a pointer-equality check would spuriously pass even if
  // WasmBase::start() had wrongly reused the destroyed root instead of creating a new one.
  // expect_forced_failure_log must be true whenever a redis_call is still genuinely outstanding
  // (no onPoolReady/onPoolFailure) at the time this is called: ~Context()'s forced-Failure
  // cleanup on the old root delivers synchronously via the new root's onConfigure()-driven drain,
  // which logs a warn -- an unexpected call under StrictMock if not declared here first.
  void recreateRootViaReconfiguration(bool expect_forced_failure_log = false) {
    plugin_handle_.reset();
    int contexts_before = root_contexts_created_;

    CreateContextFn create_root =
        [this, expect_forced_failure_log](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) {
          root_context_ = new TestContext(wasm, plugin);
          ++root_contexts_created_;
          if (expect_forced_failure_log) {
            EXPECT_CALL(*root_context_, log_(spdlog::level::warn, testing::HasSubstr("status=2")));
          }
          return root_context_;
        };
    // getOrCreateThreadLocalPlugin()'s create_root argument is only consulted by the thread-local
    // Wasm *clone factory*, which only runs the first time a thread-local Wasm is created for this
    // vm_key() -- every call afterward (including this one, since wasm_ keeps the same thread-local
    // Wasm alive throughout the test) reuses the Wasm instance's already-pinned
    // create_root_context_for_testing_ callback from the very first clone instead of the lambda
    // passed in here. Explicitly re-pin it on the live Wasm so WasmBase::start()'s
    // createRootContext() call below actually invokes *this* lambda (and so registers the
    // EXPECT_CALL above, when expect_forced_failure_log is set) rather than silently reusing the
    // original constructor's expectation-less one.
    wasm_->wasm()->setCreateContextForTesting(nullptr, create_root);
    plugin_handle_ = getOrCreateThreadLocalPlugin(wasm_, plugin_, *dispatcher_, create_root);
    wasm_ = plugin_handle_->wasmHandle();
    proxy_wasm::current_context_ = root_context_;
    ASSERT_EQ(root_contexts_created_, contexts_before + 1)
        << "reconfiguration did not create a new root context via create_root -- the old "
           "(destroyed) one may have been reused";
  }

  int root_contexts_created_ = 0;

protected:
  Stats::IsolatedStoreImpl stats_store_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Stats::ScopeSharedPtr scope_;
  testing::NiceMock<Upstream::MockClusterManager> cluster_manager_;
  testing::NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster_;
  testing::NiceMock<Network::MockClientConnection> connection_;
  testing::NiceMock<LocalInfo::MockLocalInfo> local_info_;
  testing::NiceMock<Init::MockManager> init_manager_;
  testing::NiceMock<Server::MockServerLifecycleNotifier> lifecycle_notifier_;
  RemoteAsyncDataProviderPtr remote_data_provider_;
  std::shared_ptr<Plugin> plugin_;
  WasmHandleSharedPtr wasm_;
  PluginHandleSharedPtr plugin_handle_;
  TestContext* root_context_ = nullptr; // Unowned: owned by the Wasm's context registry.
};

TEST_F(RedisCallNullVmTest, SuccessRoundTripThroughRealModule) {
  std::string result_buffer;
  auto* upstream_callbacks = startCallAndConnect(kCluster, {"PING"}, &result_buffer);
  ASSERT_NE(upstream_callbacks, nullptr);

  EXPECT_CALL(*root_context_,
              log_(spdlog::level::warn,
                   testing::AllOf(testing::HasSubstr("status=1"), testing::HasSubstr("PONG"))));

  auto reply = simpleStringReply("PONG");
  upstream_callbacks->onUpstreamData(reply, false);
  // Two hops to flush: RedisCallHandler::onRespValue's deferred completion (which calls
  // enqueueSharedQueue), then SharedQueue::enqueue's own call_on_thread post (which is what
  // actually dispatches into the module's real proxy_on_queue_ready export).
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

TEST_F(RedisCallNullVmTest, FailureRoundTripThroughRealModule) {
  std::string result_buffer;
  ASSERT_EQ(callRedis(kCluster, {"GET", "mykey"}, &result_buffer), WasmResult::Ok);

  EXPECT_CALL(*root_context_, log_(spdlog::level::warn, testing::HasSubstr("status=2")));

  thread_local_cluster_.tcp_conn_pool_.poolFailure(
      Tcp::ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
  // onPoolFailure calls onRedisCallFailure (and so enqueueSharedQueue) synchronously, so only the
  // enqueueSharedQueue call_on_thread post needs flushing.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

// A call still outstanding at the TCP layer when reconfiguration destroys its root gets
// ~Context()'s forced-Failure cleanup, which enqueues onto the shared queue against the dying
// root's own context id -- left unhandled, that notification would be silently dropped the same
// way as the already-completed case below. Unlike
// DestroyedRootReleasesReservedBudgetForReplacementRoot above (which only checks the budget
// reservation), this verifies the forced Failure is actually observable, via the replacement
// root's onConfigure()-driven drain.
TEST_F(RedisCallNullVmTest,
       OutstandingCallDestroyedByReconfigurationDeliversFailureToReplacementRoot) {
  std::string stale_result;
  // Leave this call genuinely outstanding -- no poolReady/poolFailure -- so the only thing that
  // ever resolves it is ~Context()'s forced-Failure cleanup during the reconfiguration below.
  ASSERT_EQ(callRedis(kCluster, {"GET", "stale"}, &stale_result), WasmResult::Ok);

  int contexts_before = root_contexts_created_;
  plugin_handle_.reset(); // Runs ~Context() on the old root synchronously: forces Failure now.
  CreateContextFn create_root = [this](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) {
    root_context_ = new TestContext(wasm, plugin);
    ++root_contexts_created_;
    // Set here, before returning control to WasmBase::start()/configure() -- onConfigure()'s
    // retarget-and-drain (and so the delivery this test exists to verify) runs later in that same
    // call, so an expectation set only after getOrCreateThreadLocalPlugin() returns below would be
    // too late to observe it.
    EXPECT_CALL(*root_context_, log_(spdlog::level::warn, testing::HasSubstr("status=2")));
    return root_context_;
  };
  // See StrandedCompletionDeliveredByReplacementRootWithoutNewCall's comment on why this explicit
  // re-pin is required for the new lambda's EXPECT_CALL to actually be the one WasmBase::start()
  // invokes.
  wasm_->wasm()->setCreateContextForTesting(nullptr, create_root);
  plugin_handle_ = getOrCreateThreadLocalPlugin(wasm_, plugin_, *dispatcher_, create_root);
  wasm_ = plugin_handle_->wasmHandle();
  proxy_wasm::current_context_ = root_context_;
  ASSERT_EQ(root_contexts_created_, contexts_before + 1);

  // The forced-Failure's own enqueueSharedQueue() notification (targeted at the now-destroyed old
  // root's context id) is still pending; flushing it now must be a safe no-op -- the new root's
  // onConfigure() above already delivered this result -- not a double delivery or a crash.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

// Covers ~Context()'s other branch, not exercised by the tests above: an outstanding call that had
// already connected (onPoolReady fired, so conn_data_ is set and cancel_handle_ is null) when
// reconfiguration tears its root down must close the connection, not invoke a null cancel_handle_.
TEST_F(RedisCallNullVmTest, OutstandingConnectedCallDestroyedByReconfigurationClosesConnection) {
  std::string stale_result;
  auto* upstream_callbacks = startCallAndConnect(kCluster, {"GET", "stale"}, &stale_result);
  ASSERT_NE(upstream_callbacks, nullptr);

  int contexts_before = root_contexts_created_;
  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));
  plugin_handle_.reset(); // Runs ~Context() on the old root synchronously: forces Failure now.
  CreateContextFn create_root = [this](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) {
    root_context_ = new TestContext(wasm, plugin);
    ++root_contexts_created_;
    // Set here, before returning control to WasmBase::start()/configure() -- onConfigure()'s
    // retarget-and-drain (and so the delivery this test exists to verify) runs later in that same
    // call, so an expectation set only after getOrCreateThreadLocalPlugin() returns below would be
    // too late to observe it.
    EXPECT_CALL(*root_context_, log_(spdlog::level::warn, testing::HasSubstr("status=2")));
    return root_context_;
  };
  // See StrandedCompletionDeliveredByReplacementRootWithoutNewCall's comment on why this explicit
  // re-pin is required for the new lambda's EXPECT_CALL to actually be the one WasmBase::start()
  // invokes.
  wasm_->wasm()->setCreateContextForTesting(nullptr, create_root);
  plugin_handle_ = getOrCreateThreadLocalPlugin(wasm_, plugin_, *dispatcher_, create_root);
  wasm_ = plugin_handle_->wasmHandle();
  proxy_wasm::current_context_ = root_context_;
  ASSERT_EQ(root_contexts_created_, contexts_before + 1);

  // The forced-Failure's own enqueueSharedQueue() notification (targeted at the now-destroyed old
  // root's context id) is still pending; flushing it now must be a safe no-op, not a double
  // delivery or a crash.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

// redisCall() must re-register the shared queue on every call, not just when queue_token == 0
// (i.e. the plugin instance's first-ever call), or a replacement root after reconfiguration would
// never become the dispatch target and every subsequent completion would be silently dropped.
TEST_F(RedisCallNullVmTest, ReconfiguredRootReceivesRealDispatchAfterRetargeting) {
  // Complete one call under the old root first so the re-registration path is actually exercised
  // by the post-reconfiguration call below, not just the always-registers first call.
  std::string old_result;
  EXPECT_CALL(*root_context_, log_(spdlog::level::warn, testing::HasSubstr("status=2")));
  ASSERT_EQ(callRedis(kCluster, {"GET", "before-reconfig"}, &old_result), WasmResult::Ok);
  thread_local_cluster_.tcp_conn_pool_.poolFailure(
      Tcp::ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  recreateRootViaReconfiguration();

  std::string new_result;
  EXPECT_CALL(*root_context_, log_(spdlog::level::warn, testing::HasSubstr("status=2")));
  ASSERT_EQ(callRedis(kCluster, {"GET", "after-reconfig"}, &new_result), WasmResult::Ok);
  thread_local_cluster_.tcp_conn_pool_.poolFailure(
      Tcp::ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

// A result that finished enqueuing under a root already slated for replacement, but whose
// cross-thread SharedQueue::enqueue() notification has not yet run, must still be delivered via
// onConfigure()'s retarget-and-flush on the new root -- per-call re-registration alone only
// retargets *future* enqueues, not a notification already captured against the old root's id.
TEST_F(RedisCallNullVmTest, StrandedCompletionDeliveredByReplacementRootWithoutNewCall) {
  std::string stale_result;
  ASSERT_EQ(callRedis(kCluster, {"GET", "stale"}, &stale_result), WasmResult::Ok);
  thread_local_cluster_.tcp_conn_pool_.poolFailure(
      Tcp::ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
  // Don't flush the dispatcher: the result is enqueued targeting the old root's context id, but
  // the call_on_thread notification to dispatch onQueueReady hasn't run yet.

  plugin_handle_.reset(); // Destroys the old root -- its context id is now dangling.
  int contexts_before = root_contexts_created_;
  CreateContextFn create_root = [this](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) {
    root_context_ = new TestContext(wasm, plugin);
    ++root_contexts_created_;
    // Set the expectation here, before returning control to WasmBase::start()/configure() --
    // onConfigure() (and so the drain this test exists to verify) runs later in that same call,
    // so an expectation set only after getOrCreateThreadLocalPlugin() returns below would be too
    // late to observe it.
    EXPECT_CALL(*root_context_, log_(spdlog::level::warn, testing::HasSubstr("status=2")));
    return root_context_;
  };
  // getOrCreateThreadLocalPlugin()'s create_root argument is only consulted by the thread-local
  // Wasm *clone factory*, which only runs the first time a thread-local Wasm is created for this
  // vm_key() -- every call afterward (including this one, since wasm_ keeps the same thread-local
  // Wasm alive throughout the test) reuses the Wasm instance's already-pinned
  // create_root_context_for_testing_ callback from the very first clone instead of the lambda
  // passed in here. Explicitly re-pin it on the live Wasm so WasmBase::start()'s
  // createRootContext() call below actually invokes *this* lambda (and so registers the EXPECT_CALL
  // above) rather than silently reusing the original constructor's expectation-less one.
  wasm_->wasm()->setCreateContextForTesting(nullptr, create_root);
  plugin_handle_ = getOrCreateThreadLocalPlugin(wasm_, plugin_, *dispatcher_, create_root);
  wasm_ = plugin_handle_->wasmHandle();
  proxy_wasm::current_context_ = root_context_;
  ASSERT_EQ(root_contexts_created_, contexts_before + 1);

  // The stale notification posted before reconfiguration is still pending; flushing it now must
  // be a safe no-op (getContext() on the destroyed old root's id fails) rather than a double
  // delivery or a crash -- the new root's onConfigure() above already delivered this result.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

// Unlike RedisCallNullVmTest above, which calls redis_call via a direct
// proxy_wasm::getForeignFunction() lookup, this fixture drives the plugin's own onConfigure()
// (test_data/test_redis_call_cpp.cc) to invoke proxy_call_foreign_function itself, exercising the
// real guest/host ABI boundary end to end.
class RedisCallAbiBoundaryTest : public testing::Test, public Event::TestUsingSimulatedTime {
public:
  static constexpr absl::string_view kCluster = "redis_cluster";

  class TestContext : public Context {
  public:
    using Context::Context;
    MOCK_CONTEXT_LOG_;
  };

  RedisCallAbiBoundaryTest()
      : api_(Api::createApiForTest(stats_store_, simTime())),
        dispatcher_(api_->allocateDispatcher("redis_call_abi_boundary_test")),
        scope_(stats_store_.createScope("")) {
    // Must be set up before createWasm()/getOrCreateThreadLocalPlugin() in each TEST_F below,
    // since those synchronously run the module's onConfigure() -- and so its
    // proxy_call_foreign_function("redis_call", ...) invocation.
    ON_CALL(cluster_manager_, getThreadLocalCluster(absl::string_view(kCluster)))
        .WillByDefault(testing::Return(&thread_local_cluster_));
  }

  ~RedisCallAbiBoundaryTest() override { proxy_wasm::current_context_ = nullptr; }

  static std::string serializedArgs(absl::string_view cluster,
                                    const std::vector<std::string>& command_args,
                                    uint32_t timeout_millis) {
    envoy::source::extensions::common::wasm::RedisCallArguments args;
    args.set_cluster(std::string(cluster));
    for (const auto& arg : command_args) {
      args.add_command_args(arg);
    }
    args.set_timeout_millis(timeout_millis);
    std::string serialized;
    std::ignore = args.SerializeToString(&serialized);
    return serialized;
  }

protected:
  Stats::IsolatedStoreImpl stats_store_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Stats::ScopeSharedPtr scope_;
  testing::NiceMock<Upstream::MockClusterManager> cluster_manager_;
  testing::NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster_;
  testing::NiceMock<LocalInfo::MockLocalInfo> local_info_;
  testing::NiceMock<Init::MockManager> init_manager_;
  testing::NiceMock<Server::MockServerLifecycleNotifier> lifecycle_notifier_;
  RemoteAsyncDataProviderPtr remote_data_provider_;
  std::shared_ptr<Plugin> plugin_;
  WasmHandleSharedPtr wasm_;
  PluginHandleSharedPtr plugin_handle_;
  TestContext* root_context_ = nullptr; // Unowned: owned by the Wasm's context registry.
};

TEST_F(RedisCallAbiBoundaryTest, SuccessStartsCallThroughRealAbi) {
  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  plugin_config.mutable_vm_config()->set_vm_id("redis_call_abi_boundary_test");
  plugin_config.mutable_vm_config()->set_runtime("envoy.wasm.runtime.null");
  plugin_config.mutable_vm_config()->mutable_code()->mutable_local()->set_inline_bytes(
      "RedisCallNullPlugin");
  plugin_config.mutable_configuration()->set_value(serializedArgs(kCluster, {"PING"}, 1000));
  plugin_ = std::make_shared<Plugin>(plugin_config, local_info_);

  CreateContextFn create_root = [this](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) {
    root_context_ = new TestContext(wasm, plugin);
    // Set here, before returning control to WasmBase::start()/configure() -- onConfigure() (and
    // so the real ABI call this test exists to verify) runs later in that same call, so an
    // expectation set only after getOrCreateThreadLocalPlugin() returns below would be too late
    // to observe it.
    EXPECT_CALL(*root_context_,
                log_(spdlog::level::warn, testing::HasSubstr("start_ok queue_token=")));
    return root_context_;
  };
  createWasm(
      plugin_, scope_, cluster_manager_, init_manager_, *dispatcher_, *api_, lifecycle_notifier_,
      remote_data_provider_, [this](WasmHandleSharedPtr wasm) { wasm_ = wasm; }, create_root);
  plugin_handle_ = getOrCreateThreadLocalPlugin(wasm_, plugin_, *dispatcher_, create_root);
  wasm_ = plugin_handle_->wasmHandle();
  proxy_wasm::current_context_ = root_context_;
}

TEST_F(RedisCallAbiBoundaryTest, EmptyCommandArgsIsBadArgumentThroughRealAbi) {
  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  plugin_config.mutable_vm_config()->set_vm_id("redis_call_abi_boundary_test");
  plugin_config.mutable_vm_config()->set_runtime("envoy.wasm.runtime.null");
  plugin_config.mutable_vm_config()->mutable_code()->mutable_local()->set_inline_bytes(
      "RedisCallNullPlugin");
  plugin_config.mutable_configuration()->set_value(serializedArgs(kCluster, {}, 1000));
  plugin_ = std::make_shared<Plugin>(plugin_config, local_info_);

  CreateContextFn create_root = [this](Wasm* wasm, const std::shared_ptr<Plugin>& plugin) {
    root_context_ = new TestContext(wasm, plugin);
    EXPECT_CALL(*root_context_, log_(spdlog::level::warn, testing::HasSubstr("start_status=2")));
    return root_context_;
  };
  createWasm(
      plugin_, scope_, cluster_manager_, init_manager_, *dispatcher_, *api_, lifecycle_notifier_,
      remote_data_provider_, [this](WasmHandleSharedPtr wasm) { wasm_ = wasm; }, create_root);
  plugin_handle_ = getOrCreateThreadLocalPlugin(wasm_, plugin_, *dispatcher_, create_root);
  wasm_ = plugin_handle_->wasmHandle();
  proxy_wasm::current_context_ = root_context_;
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
