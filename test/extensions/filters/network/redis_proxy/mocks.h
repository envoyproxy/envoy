#pragma once

#include <cstdint>
#include <list>
#include <string>
#include <utility>

#include "envoy/stream_info/stream_info.h"

#include "source/extensions/common/redis/cluster_refresh_manager.h"
#include "source/extensions/filters/network/common/redis/client.h"
#include "source/extensions/filters/network/common/redis/codec_impl.h"
#include "source/extensions/filters/network/common/redis/fault.h"
#include "source/extensions/filters/network/redis_proxy/command_splitter.h"
#include "source/extensions/filters/network/redis_proxy/conn_pool.h"
#include "source/extensions/filters/network/redis_proxy/external_auth.h"
#include "source/extensions/filters/network/redis_proxy/router.h"
#include "source/extensions/filters/network/redis_proxy/subscription_registry.h"

#include "test/test_common/printers.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

// Shared mock for the registry <-> conn-pool upstream subscription interface (was defined
// verbatim in both subscription_registry_test and command_splitter_impl_test).
class MockUpstreamSubscriptionCallbacks : public UpstreamSubscriptionCallbacks {
public:
  ~MockUpstreamSubscriptionCallbacks() override = default;

  MOCK_METHOD(Upstream::HostConstSharedPtr, chooseUpstreamHostForChannel,
              (const std::string& channel), (override));
  MOCK_METHOD(bool, hostServesChannelSlot,
              (const std::string& channel, const Upstream::HostConstSharedPtr& host), (override));
  MOCK_METHOD(bool, sendUpstreamSsubscribeToHost,
              (const std::string& channel,
               Common::Redis::Client::PushMessageCallbacks& push_callbacks,
               const Upstream::HostConstSharedPtr& host),
              (override));
  MOCK_METHOD(UpstreamSubscriptionCallbacks::SunsubscribeResult, sendUpstreamSunsubscribe,
              (const std::string& channel, Upstream::HostConstSharedPtr host), (override));
  MOCK_METHOD(void, scheduleResubscribe, (std::chrono::milliseconds delay), (override));
  MOCK_METHOD(bool, resubscribeTimerPending, (), (const, override));
  MOCK_METHOD(void, cancelResubscribeTimer, (), (override));
  MOCK_METHOD(void, requestTopologyRefresh, (), (override));
  MOCK_METHOD(bool, retireSubscriptionConnectionIfIdle, (const Upstream::HostConstSharedPtr& host),
              (override));
  MOCK_METHOD(void, closeSubscriptionConnection, (const Upstream::HostConstSharedPtr& host),
              (override));
};

class MockRouter : public Router {
public:
  MockRouter(RouteSharedPtr route);
  ~MockRouter() override;

  MOCK_METHOD(RouteSharedPtr, upstreamPool,
              (std::string & key, const StreamInfo::StreamInfo& stream_info));
  MOCK_METHOD(void, initializeReadFilterCallbacks, (Network::ReadFilterCallbacks * callbacks));
  RouteSharedPtr route_;
};

class MockRoute : public Route {
public:
  MockRoute(ConnPool::InstanceSharedPtr);
  ~MockRoute() override;

  MOCK_METHOD(ConnPool::InstanceSharedPtr, upstream, (const std::string&), (const));
  MOCK_METHOD(ConnPool::InstanceSharedPtr, pubsubUpstream, (), (const));
  MOCK_METHOD(const MirrorPolicies&, mirrorPolicies, (), (const));
  ConnPool::InstanceSharedPtr conn_pool_;
  MirrorPolicies policies_;
};

class MockMirrorPolicy : public MirrorPolicy {
public:
  MockMirrorPolicy(ConnPool::InstanceSharedPtr);
  ~MockMirrorPolicy() override = default;

  MOCK_METHOD(ConnPool::InstanceSharedPtr, upstream, (), (const));
  MOCK_METHOD(bool, shouldMirror, (const std::string&), (const));

  ConnPool::InstanceSharedPtr conn_pool_;
};

class MockFaultManager : public Common::Redis::FaultManager {
public:
  MockFaultManager();
  MockFaultManager(const MockFaultManager& other);
  ~MockFaultManager() override;

  MOCK_METHOD(const Common::Redis::Fault*, getFaultForCommand, (const std::string&), (const));
};

namespace ConnPool {

class MockPoolCallbacks : public PoolCallbacks {
public:
  MockPoolCallbacks();
  ~MockPoolCallbacks() override;

  void onResponse(Common::Redis::RespValuePtr&& value) override { onResponse_(value); }
  void onFailure() override { onFailure_(); }

  MOCK_METHOD(void, onResponse_, (Common::Redis::RespValuePtr & value));
  MOCK_METHOD(void, onFailure_, ());
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance() override;

  uint16_t shardSize() override { return shardSize_(); }

  Common::Redis::Client::PoolRequest* makeRequest(const std::string& hash_key,
                                                  RespVariant&& request, PoolCallbacks& callbacks,
                                                  Common::Redis::Client::Transaction&) override {
    return makeRequest_(hash_key, request, callbacks);
  }

  Common::Redis::Client::PoolRequest*
  makeRequestToShard(uint16_t shard_index, RespVariant&& request, PoolCallbacks& callbacks,
                     Common::Redis::Client::Transaction&) override {
    return makeRequestToShard_(shard_index, request, callbacks);
  }

  MOCK_METHOD(uint16_t, shardSize_, ());
  MOCK_METHOD(Common::Redis::Client::PoolRequest*, makeRequest_,
              (const std::string& hash_key, RespVariant& request, PoolCallbacks& callbacks));
  MOCK_METHOD(Common::Redis::Client::PoolRequest*, makeRequestToShard_,
              (uint16_t shard_index, RespVariant& request, PoolCallbacks& callbacks));
  MOCK_METHOD(bool, onRedirection, ());

  // UpstreamSubscriptionCallbacks not needed in mock — returns nullptr for registry.
};
} // namespace ConnPool

namespace CommandSplitter {

class MockSplitRequest : public SplitRequest {
public:
  MockSplitRequest();
  ~MockSplitRequest() override;

  MOCK_METHOD(void, cancel, ());
};

// No-op implementation of the PubsubSession hooks: a SplitCallbacks double that carries no
// subscription state. The command-lookup speed test's NoOpSplitCallbacks and MockSplitCallbacks
// inherit these hooks instead of re-stubbing the block in each file.
class NoOpPubsubSession : public PubsubSession {
public:
  RedisProxy::DownstreamSubscriberPtr downstreamSubscriber() override { return nullptr; }
  void setSubscriptionRegistry(const RedisProxy::SubscriptionRegistryPtr&) override {}
  RedisProxy::SubscriptionRegistryPtr subscriptionRegistryForChannel(const std::string&) override {
    return nullptr;
  }
  void bindSubscriptionRegistryForChannel(const std::string&,
                                          const RedisProxy::SubscriptionRegistryPtr&) override {}
  void unbindSubscriptionRegistryForChannel(const std::string&) override {}
  uint64_t unsubscribeChannelAcrossRegistries(const std::string&,
                                              const RedisProxy::DownstreamSubscriberPtr&,
                                              std::vector<Common::Redis::RespValue>&) override {
    return 0;
  }
  void onPubsubSubscriptionChange(int64_t) override {}
};

class MockSplitCallbacks : public SplitCallbacks, public NoOpPubsubSession {
public:
  MockSplitCallbacks();
  ~MockSplitCallbacks() override;

  // respond() is the single virtual terminal (gmock can't match a move-only vector directly, so
  // forward to by-ref mocks). Route by frame count to preserve the older by-value assertion
  // surfaces:
  //  * one frame -> onResponse_ — the overwhelmingly common single-reply path (the splitter's
  //  onResponse() sugar; also a single-channel pub/sub error, which is genuinely
  //  indistinguishable from any other single reply at this seam).
  //  * >= 2 frames -> respond_ — a multi-channel SUBSCRIBE emitting several per-channel -ERRs.
  //  * zero frames -> silent, matching the old completePendingRequest() no-op: a completion that
  //  carries no downstream frame — e.g. a bare UNSUBSCRIBE with nothing to ack, which respond({})s
  //  only to complete the request — pins onResponse_ Times(0) and needs no expectation;
  //  TestSplitCallbacks still records the completion via completed_pending_request_.
  void respond(CommandSplitter::RespValueFrames&& frames) override {
    if (frames.size() == 1) {
      onResponse_(frames[0]);
    } else if (frames.size() > 1) {
      respond_(frames);
    }
  }
  Common::Redis::Client::Transaction& transaction() override { return transaction_; }
  void setDownstreamRespVersion(uint32_t version) override { downstream_resp_version_ = version; }
  Common::Redis::RespProtocolVersion protocolVersion() const override { return protocol_version_; }
  bool shardedPublishEnabled() const override { return sharded_publish_enabled_; }

  MOCK_METHOD(bool, connectionAllowed, ());
  MOCK_METHOD(void, onQuit, ());
  MOCK_METHOD(void, onAuth, (const std::string& password));
  MOCK_METHOD(void, onAuth, (const std::string& username, const std::string& password));
  MOCK_METHOD(void, onResponse_, (Common::Redis::RespValuePtr & value));
  MOCK_METHOD(void, respond_, (CommandSplitter::RespValueFrames & frames));

  uint32_t currentDownstreamRespVersion() const override { return downstream_resp_version_; }
  // The mock returns whatever ``inline_auth_attempt_`` is set to. Tests that exercise
  // HELLO N AUTH ... must set inline_auth_attempt_ explicitly before driving the request —
  // the default Denied keeps tests that do not exercise HELLO AUTH safe (a stray
  // attempt fails loudly with a WRONGPASS reply rather than silently emitting nothing,
  // which the deferred ``ImplOwnsResponse`` case would).
  AuthAttempt attemptDownstreamAuthInline(const std::string& username, const std::string& password,
                                          uint32_t requested_version) override {
    ++inline_auth_attempt_count_;
    last_inline_auth_username_ = username;
    last_inline_auth_password_ = password;
    last_inline_auth_requested_version_ = requested_version;
    return inline_auth_attempt_;
  }
  // The deferred HELLO-AUTH version is exercised through the real ProxyFilter::PendingRequest in
  // proxy_filter_test; no MockSplitCallbacks test drives it, so this stub simply reports "none".
  std::optional<uint32_t> takePendingHelloAuthVersion() override { return std::nullopt; }

  uint32_t downstream_resp_version_{2};
  // Defaults to RESP2 listener — matches the proto default. Tests covering the RESP3-listener
  // path drive this to Resp3.
  Common::Redis::RespProtocolVersion protocol_version_{Common::Redis::RespProtocolVersion::Resp2};
  // Opt-in sharded publish. Defaults off (matches the proto default); PublishRequest tests that
  // assert the SPUBLISH rewrite set this true before driving the request.
  bool sharded_publish_enabled_{false};
  AuthAttempt inline_auth_attempt_{AuthAttempt::Denied};
  std::string last_inline_auth_username_;
  std::string last_inline_auth_password_;
  uint32_t last_inline_auth_requested_version_{0};
  // Number of times attemptDownstreamAuthInline was invoked. Lets tests assert the inline-auth
  // path was NOT taken (e.g. a duplicate HELLO option must error before any auth attempt).
  int inline_auth_attempt_count_{0};

  // MockSplitCallbacks is itself the pub/sub session for tests that drive subscribe/unsubscribe.
  // downstreamSubscriber / setSubscriptionRegistry / onPubsubSubscriptionChange are the inherited
  // NoOpPubsubSession no-ops; only pubsub() and the real cross-registry unsubscribe differ.
  PubsubSession* pubsub() override { return this; }
  // Mirrors ProxyFilter's cross-registry unsubscribe FALLBACK path. This mock tracks no per-channel
  // owner (subscriptionRegistryForChannel returns null), so the production owner-first shortcut
  // never applies and every unsubscribe takes the sweep: clean up in EVERY tracked registry, do NOT
  // break at the first count drop. A channel normally lives in one registry, but were an earlier
  // defect to duplicate it across registries, an early break would strand the copies; a full sweep
  // converges the state. The subscriber's post-sweep total is the downstream ack count.
  uint64_t unsubscribeChannelAcrossRegistries(
      const std::string& channel, const RedisProxy::DownstreamSubscriberPtr& subscriber,
      std::vector<Common::Redis::RespValue>& preserved_acks) override {
    for (const auto& weak_reg : tracked_registries_) {
      auto reg = weak_reg.lock();
      if (!reg) {
        continue;
      }
      reg->unsubscribe(absl::MakeConstSpan(&channel, 1), subscriber, &preserved_acks);
    }
    return subscriber->totalSubscriptionCount();
  }

  std::vector<std::weak_ptr<RedisProxy::SubscriptionRegistry>> tracked_registries_;

private:
  Common::Redis::Client::NoOpTransaction transaction_;
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance() override;

  SplitRequestPtr makeRequest(Common::Redis::RespValuePtr&& request, SplitCallbacks& callbacks,
                              Event::Dispatcher& dispatcher,
                              const StreamInfo::StreamInfo& stream_info) override {
    return SplitRequestPtr{makeRequest_(*request, callbacks, dispatcher, stream_info)};
  }
  MOCK_METHOD(SplitRequest*, makeRequest_,
              (const Common::Redis::RespValue& request, SplitCallbacks& callbacks,
               Event::Dispatcher& dispatcher, const StreamInfo::StreamInfo& stream_info));
};

} // namespace CommandSplitter

namespace ExternalAuth {

class MockExternalAuthClient : public ExternalAuthClient {
public:
  MockExternalAuthClient();
  ~MockExternalAuthClient() override;

  // ExtAuthz::Client
  MOCK_METHOD(void, cancel, ());
  MOCK_METHOD(void, authenticateExternal,
              (AuthenticateCallback & callback, CommandSplitter::SplitCallbacks& pending_request,
               const StreamInfo::StreamInfo& stream_info, std::string username,
               std::string password));
};

class MockAuthenticateCallback : public AuthenticateCallback {
public:
  MockAuthenticateCallback();
  ~MockAuthenticateCallback() override;

  void onAuthenticateExternal(CommandSplitter::SplitCallbacks& request,
                              AuthenticateResponsePtr&& response) override {
    onAuthenticateExternal_(request, response);
  }

  MOCK_METHOD(void, onAuthenticateExternal_,
              (CommandSplitter::SplitCallbacks & request, AuthenticateResponsePtr& response));
};

} // namespace ExternalAuth
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
