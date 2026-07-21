#pragma once

#include <chrono>

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/stats/timespan.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/hash.h"
#include "source/common/network/filter_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/singleton/const_singleton.h"
#include "source/common/upstream/load_balancer_context_base.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/filters/network/common/redis/aws_iam_authenticator_impl.h"
#include "source/extensions/filters/network/common/redis/client.h"
#include "source/extensions/filters/network/common/redis/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {
namespace Client {

// TODO(mattklein123): Circuit breaking
// TODO(rshriram): Fault injection

struct RedirectionValues {
  const std::string ASK = "ASK";
  const std::string MOVED = "MOVED";
  const std::string CLUSTER_DOWN = "CLUSTERDOWN";
};

using RedirectionResponse = ConstSingleton<RedirectionValues>;

class ConfigImpl : public Config {
public:
  ConfigImpl(
      const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::ConnPoolSettings&
          config);

  bool disableOutlierEvents() const override { return false; }
  std::chrono::milliseconds opTimeout() const override { return op_timeout_; }
  bool enableHashtagging() const override { return enable_hashtagging_; }
  bool enableRedirection() const override { return enable_redirection_; }
  uint32_t maxBufferSizeBeforeFlush() const override { return max_buffer_size_before_flush_; }
  std::chrono::milliseconds bufferFlushTimeoutInMs() const override {
    return buffer_flush_timeout_;
  }
  uint32_t maxUpstreamUnknownConnections() const override {
    return max_upstream_unknown_connections_;
  }
  bool enableCommandStats() const override { return enable_command_stats_; }
  ReadPolicy readPolicy() const override { return read_policy_; }
  bool connectionRateLimitEnabled() const override { return connection_rate_limit_enabled_; }
  uint32_t connectionRateLimitPerSec() const override { return connection_rate_limit_per_sec_; }

private:
  const std::chrono::milliseconds op_timeout_;
  const bool enable_hashtagging_;
  const bool enable_redirection_;
  const uint32_t max_buffer_size_before_flush_;
  const std::chrono::milliseconds buffer_flush_timeout_;
  const uint32_t max_upstream_unknown_connections_;
  const bool enable_command_stats_;
  ReadPolicy read_policy_;
  bool connection_rate_limit_enabled_;
  uint32_t connection_rate_limit_per_sec_;
};

class ClientImpl : public Client,
                   public DecoderCallbacks,
                   public Network::ConnectionCallbacks,
                   public Logger::Loggable<Logger::Id::redis> {
public:
  // Static factory. Does not take auth_username/auth_password — both flow into ClientImpl via
  // initialize(), which is called by ClientFactoryImpl::create after this returns.
  static ClientPtr
  create(Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher, EncoderPtr&& encoder,
         DecoderFactory& decoder_factory, const ConfigSharedPtr& config,
         const RedisCommandStatsSharedPtr& redis_command_stats, Stats::Scope& scope,
         bool is_transaction_client,
         std::optional<envoy::extensions::filters::network::redis_proxy::v3::AwsIam> aws_iam_config,
         std::optional<Common::Redis::AwsIamAuthenticator::AwsIamAuthenticatorSharedPtr>
             aws_iam_authenticator,
         Common::Redis::RespProtocolVersion upstream_protocol_version,
         OptRef<Stats::Counter> upstream_resp3_hello_failure);

  ClientImpl(
      Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher, EncoderPtr&& encoder,
      DecoderFactory& decoder_factory, const ConfigSharedPtr& config,
      const RedisCommandStatsSharedPtr& redis_command_stats, Stats::Scope& scope,
      bool is_transaction_client,
      std::optional<envoy::extensions::filters::network::redis_proxy::v3::AwsIam> aws_iam_config,
      std::optional<Common::Redis::AwsIamAuthenticator::AwsIamAuthenticatorSharedPtr>
          aws_iam_authenticator,
      Common::Redis::RespProtocolVersion upstream_protocol_version,
      OptRef<Stats::Counter> upstream_resp3_hello_failure);
  ~ClientImpl() override;

  // Client
  void addConnectionCallbacks(Network::ConnectionCallbacks& callbacks) override {
    connection_->addConnectionCallbacks(callbacks);
  }
  void close() override;
  PoolRequest* makeRequest(const RespValue& request, ClientCallbacks& callbacks) override;
  // Active for the conn pool's drain-vs-close decision: in-flight or init-held work.
  bool active() override { return hasOutstandingWork(); }
  void flushBufferAndResetTimer();
  void initialize(const std::string& auth_username, const std::string& auth_password) override;

private:
  friend class RedisClientImplTest;

  // Init state machine for HELLO 3 / AUTH / READONLY / IAM-token negotiation. initialize()
  // either snaps to Ready (RESP2 + no IAM, any read policy — AUTH/READONLY go out
  // fire-and-forget there) or walks the four negotiation states; isUserTrafficGated() gates
  // makeRequest during those.
  enum class InitState : uint8_t {
    NotStarted,         // ctor default; initialize() has not been called yet
    WaitingForAwsToken, // AWS IAM token fetch in flight; held queue gates user requests
    AwaitingHello,      // HELLO 3 sent; awaiting reply (RESP3)
    AwaitingReadonly,   // credentials acked (HELLO or AUTH); READONLY sent; awaiting reply
    AwaitingAuth,       // RESP2 + IAM: AUTH sent post-token; awaiting reply
    Ready,              // negotiation complete (or none required); user traffic flows
    Failed,             // any failure; held queue drained with onFailure()
  };

  // True only during the four async negotiation states. NotStarted is not gated: init
  // commands go through makeRequestInternal (which never consults this gate), so gating
  // NotStarted would only affect a user makeRequest issued before initialize() — a sequence
  // the factory contract already forbids.
  static constexpr bool isUserTrafficGated(InitState s) {
    return s == InitState::WaitingForAwsToken || s == InitState::AwaitingHello ||
           s == InitState::AwaitingReadonly || s == InitState::AwaitingAuth;
  }

  struct UpstreamReadFilter : public Network::ReadFilterBaseImpl {
    UpstreamReadFilter(ClientImpl& parent) : parent_(parent) {}

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data, bool) override {
      parent_.onData(data);
      return Network::FilterStatus::Continue;
    }

    ClientImpl& parent_;
  };

  class HeldUserRequest;

  struct PendingRequest : public PoolRequest {
    PendingRequest(ClientImpl& parent, ClientCallbacks& callbacks, Stats::StatName stat_name);
    ~PendingRequest() override;

    // PoolRequest
    void cancel() override;

    ClientImpl& parent_;
    ClientCallbacks& callbacks_;
    Stats::StatName command_;
    bool canceled_{};
    // Set at replay time when ``callbacks_`` is a client-owned HeldUserRequest wrapper; null for
    // ordinary requests whose ``callbacks_`` is an external object the conn pool may free at
    // cancel time. Only the wrapper gets a cancel-complete cleanup call — dispatching through
    // ``callbacks_`` on a canceled ordinary request would touch freed memory.
    HeldUserRequest* held_wrapper_{nullptr};
    Stats::TimespanPtr aggregate_request_timer_;
    Stats::TimespanPtr command_request_timer_;
  };

  // Wrapper for a user request held during init: PoolRequest for cancel, ClientCallbacks for
  // replay-time response forwarding. Removed via removeHeldUserRequest, failHeldUserRequests,
  // or onCancelComplete. Post-replay cancel must not self-destruct (the live
  // PendingRequest::callbacks_ reference would dangle); pre-replay cancel erases immediately.
  class HeldUserRequest : public PoolRequest, public ClientCallbacks {
  public:
    HeldUserRequest(ClientImpl& parent, ClientCallbacks& original)
        : parent_(parent), original_callbacks_(original) {}

    // PoolRequest
    void cancel() override;

    // ClientCallbacks
    void onResponse(Common::Redis::RespValuePtr&& value) override;
    void onFailure() override;
    void onRedirection(Common::Redis::RespValuePtr&& value, const std::string& host_address,
                       bool ask_redirection) override;
    // Cleanup hook invoked directly by the live PendingRequest's canceled branch (via
    // PendingRequest::held_wrapper_), not through the ClientCallbacks vtable — the wrapper self-
    // destructs here once the live PendingRequest that referenced it has been popped.
    void onCancelComplete();

    ClientImpl& parent_;
    ClientCallbacks& original_callbacks_;
    Common::Redis::RespValuePtr request_;   // owned deep copy
    PendingRequest* live_request_{nullptr}; // set during replay
    bool canceled_{false};
    std::list<std::unique_ptr<HeldUserRequest>>::iterator self_iter_; // O(1) erase
  };

  // ClientCallbacks for the HELLO 3 init reply. Validates Map containing
  // proto=3; on success transitions to AwaitingReadonly (when read_policy
  // != Primary) or Ready; on failure increments upstream_resp3_hello_failure_
  // (when the OptRef is engaged) and triggers onInitFailure.
  class Hello3InitCallbacks : public ClientCallbacks {
  public:
    explicit Hello3InitCallbacks(ClientImpl& parent) : parent_(parent) {}
    void onResponse(Common::Redis::RespValuePtr&& value) override;
    void onFailure() override;
    void onRedirection(Common::Redis::RespValuePtr&& value, const std::string& host_address,
                       bool ask_redirection) override;

  private:
    ClientImpl& parent_;
  };

  // ClientCallbacks for READONLY init reply (sent only after HELLO success
  // when read_policy != Primary). On success transitions to Ready; on failure
  // triggers onInitFailure (no HELLO counter increment since the failure
  // is on a different command).
  class ReadOnlyInitCallbacks : public ClientCallbacks {
  public:
    explicit ReadOnlyInitCallbacks(ClientImpl& parent) : parent_(parent) {}
    void onResponse(Common::Redis::RespValuePtr&& value) override;
    void onFailure() override;
    void onRedirection(Common::Redis::RespValuePtr&& value, const std::string& host_address,
                       bool ask_redirection) override;

  private:
    ClientImpl& parent_;
  };

  // ClientCallbacks for AUTH reply during the RESP2 + AWS IAM init path.
  // On success transitions to Ready; on failure triggers onInitFailure.
  class AwsIamAuthInitCallbacks : public ClientCallbacks {
  public:
    explicit AwsIamAuthInitCallbacks(ClientImpl& parent) : parent_(parent) {}
    void onResponse(Common::Redis::RespValuePtr&& value) override;
    void onFailure() override;
    void onRedirection(Common::Redis::RespValuePtr&& value, const std::string& host_address,
                       bool ask_redirection) override;

  private:
    ClientImpl& parent_;
  };

  // Lifetime guard for the IAM token-fetch callback (UAF avoidance): the closure holds a
  // weak_ptr<AwsInitCallbackState>; ClientImpl::~ClientImpl clears parent so an on-fire
  // weak.lock()->parent check returns nullptr and the closure becomes a no-op.
  struct AwsInitCallbackState {
    ClientImpl* parent;
  };

  void onConnectOrOpTimeout();
  void onData(Buffer::Instance& data);
  void putOutlierEvent(Upstream::Outlier::Result result);

  // DecoderCallbacks
  void onRespValue(RespValuePtr&& value) override;

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  // Init state machine + held-queue helpers (see InitState above).
  PoolRequest* makeRequestInternal(const RespValue& request, ClientCallbacks& callbacks);
  void setInitState(InitState new_state);
  void replayHeldUserRequests();
  void failHeldUserRequests();
  void removeHeldUserRequest(HeldUserRequest* held);
  // True while any request-related work is outstanding: an upstream request is in flight
  // (including init requests such as HELLO/AUTH/READONLY in pending_requests_), user requests
  // are queued during init (held_user_requests_), or the AWS IAM token fetch is pending.
  // The token wait is the one init stage with no wire request to track, and it must still be
  // bounded by the connect/op timer — otherwise a hung credentials provider leaves a
  // half-initialized connection in the pool with every timer disabled once its held requests
  // are canceled. Drives whether the post-connect op timeout should be armed.
  bool hasOutstandingWork() const {
    return init_state_ == InitState::WaitingForAwsToken || !pending_requests_.empty() ||
           !held_user_requests_.empty();
  }
  // RAII batching guard for multi-request init/replay sequences: suppresses makeRequestInternal's
  // auto-flush for its scope and performs one explicit flush on destruction, so the batch hits
  // the wire as a single write and the re-enable + flush invariant cannot be dropped by an
  // early return or a future call site.
  struct BatchFlushGuard {
    explicit BatchFlushGuard(ClientImpl& parent) : parent_(parent) {
      parent_.queue_enabled_ = true;
    }
    ~BatchFlushGuard() {
      parent_.queue_enabled_ = false;
      parent_.flushBufferAndResetTimer();
    }
    ClientImpl& parent_;
  };

  void sendResp3InitCommands(const std::string& auth_username, const std::string& auth_password);
  void sendReadonlyInit();
  void onInitStepSuccess(InitState completed_step);
  void onInitFailure();
  // RESP2 + AWS IAM and RESP3 + AWS IAM both route through here from initialize(): registers a
  // pending-credentials callback (or fires synchronously when the IAM token is already cached)
  // that resumes via onAwsCredentialsReady once the token arrives.
  void sendAwsIamAuth(
      const std::string& auth_username,
      const envoy::extensions::filters::network::redis_proxy::v3::AwsIam& aws_iam_config);
  void onAwsCredentialsReady(
      const std::string& auth_username,
      const envoy::extensions::filters::network::redis_proxy::v3::AwsIam& aws_iam_config);

  Upstream::HostConstSharedPtr host_;
  Network::ClientConnectionPtr connection_;
  EncoderPtr encoder_;
  Buffer::OwnedImpl encoder_buffer_;
  DecoderPtr decoder_;
  const ConfigSharedPtr config_;
  std::list<PendingRequest> pending_requests_;
  Event::TimerPtr connect_or_op_timer_;
  bool connected_{};
  Event::TimerPtr flush_timer_;
  Envoy::TimeSource& time_source_;
  const RedisCommandStatsSharedPtr redis_command_stats_;
  Stats::Scope& scope_;
  bool is_transaction_client_;
  bool queue_enabled_{false};
  // Set by close() — the deliberate-teardown entry point (pool drain, transaction close,
  // downstream churn). Hello3InitCallbacks::onFailure consults it so that tearing down a
  // connection with a HELLO 3 still in flight does not count toward
  // upstream_resp3_hello_failure. Deliberately NOT set for other locally-initiated closes
  // (connect/op timeout, decode protocol error): those are upstream-caused negotiation
  // failures and must keep counting, per the counter's "network failure" contract.
  bool deliberate_close_{false};
  std::optional<envoy::extensions::filters::network::redis_proxy::v3::AwsIam> aws_iam_config_;
  std::optional<Common::Redis::AwsIamAuthenticator::AwsIamAuthenticatorSharedPtr>
      aws_iam_authenticator_;
  // Per-connection upstream RESP version, captured from the conn pool at create time. Drives the
  // RESP3 branch in ClientImpl::initialize. ``Resp2`` keeps the legacy no-HELLO behavior.
  const Common::Redis::RespProtocolVersion upstream_protocol_version_;
  // Optional HELLO 3 failure counter. Engaged when the creating conn pool owns a per-cluster
  // stat for it; callers without one (health checker, cluster discovery — both RESP2-only)
  // pass an empty OptRef and the Hello3InitCallbacks increment sites skip it.
  OptRef<Stats::Counter> upstream_resp3_hello_failure_;
  // Init state — see enum comment.
  InitState init_state_{InitState::NotStarted};
  // Pre-init / IAM-pending user requests, replayed in FIFO when state→Ready or drained on Failed.
  std::list<std::unique_ptr<HeldUserRequest>> held_user_requests_;
  // Init reply callbacks. Constructed once with a back-pointer to *this; reused across the
  // single HELLO/READONLY/AUTH dispatch each connection. Stored as members so their addresses
  // remain stable for the lifetime of the live PendingRequest that references them.
  Hello3InitCallbacks hello_init_callbacks_;
  ReadOnlyInitCallbacks readonly_init_callbacks_;
  AwsIamAuthInitCallbacks awsiam_auth_init_callbacks_;
  // AWS IAM token-fetch callback lifetime guard — see struct comment.
  std::shared_ptr<AwsInitCallbackState> aws_init_state_;
};

class ClientFactoryImpl : public ClientFactory, public Logger::Loggable<Logger::Id::redis> {
public:
  // RedisProxy::ConnPool::ClientFactoryImpl
  ClientPtr
  create(Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher,
         const ConfigSharedPtr& config, const RedisCommandStatsSharedPtr& redis_command_stats,
         Stats::Scope& scope, const std::string& auth_username, const std::string& auth_password,
         bool is_transaction_client,
         std::optional<envoy::extensions::filters::network::redis_proxy::v3::AwsIam> aws_iam_config,
         std::optional<Common::Redis::AwsIamAuthenticator::AwsIamAuthenticatorSharedPtr>
             aws_iam_authenticator,
         Common::Redis::RespProtocolVersion upstream_protocol_version,
         OptRef<Stats::Counter> upstream_resp3_hello_failure) override;

  static ClientFactoryImpl instance_;

private:
  DecoderFactoryImpl decoder_factory_;
};

} // namespace Client
} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
