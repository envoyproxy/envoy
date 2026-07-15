#include "source/extensions/filters/network/redis_proxy/proxy_filter.h"

#include <openssl/mem.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/common/assert.h"
#include "source/common/common/cleanup.h"
#include "source/common/common/fmt.h"
#include "source/common/config/datasource.h"
#include "source/common/config/utility.h"
#include "source/extensions/filters/network/common/redis/supported_commands.h"
#include "source/extensions/filters/network/common/redis/utility.h"
#include "source/extensions/filters/network/redis_proxy/command_splitter.h"
#include "source/extensions/filters/network/redis_proxy/command_splitter_impl.h"

#include "absl/strings/ascii.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace {

// Proto-to-codec conversion. The proto enum encodes RESP3 as 1; never pass it to
// ``Common::Redis::toRespProtocolVersion(uint32_t)`` (which expects the wire integer 3).
Common::Redis::RespProtocolVersion toCodecRespVersion(
    envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::ProtocolVersion v) {
  return v == envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::RESP3
             ? Common::Redis::RespProtocolVersion::Resp3
             : Common::Redis::RespProtocolVersion::Resp2;
}

} // namespace

// UAF-safe bridge from a shared DownstreamSubscriber back to its per-connection ProxyFilter. The
// subscriber holds this only weakly, so once the owning filter is destroyed (its sole shared_ptr
// dropped) a late deliverMessage sees an expired weak_ptr and falls back to a direct write.
// detach() additionally severs the back-pointer synchronously from the filter's destructor,
// covering the rare case of a delivery re-entered while the filter is being torn down but its
// members are not yet gone.
class ProxyFilterPushSink : public PushOrderingSink {
public:
  explicit ProxyFilterPushSink(ProxyFilter& filter) : filter_(&filter) {}
  void enqueueOrderedPush(Buffer::Instance& encoded) override {
    if (filter_ != nullptr) {
      filter_->enqueueOrderedPush(encoded);
    } else {
      // Filter gone (connection torn down): dropping the push is correct, but honor the move
      // contract by consuming the buffer so the caller need not drain it itself.
      encoded.drain(encoded.length());
    }
  }
  void detach() { filter_ = nullptr; }

private:
  ProxyFilter* filter_;
};

ProxyFilterConfig::ProxyFilterConfig(
    const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy& config,
    Stats::Scope& scope, const Network::DrainDecision& drain_decision, Runtime::Loader& runtime,
    Api::Api& api, TimeSource& time_source,
    Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory& cache_manager_factory)
    : drain_decision_(drain_decision), runtime_(runtime),
      stat_prefix_(fmt::format("redis.{}.", config.stat_prefix())),
      stats_(generateStats(stat_prefix_, scope)),
      downstream_auth_username_(THROW_OR_RETURN_VALUE(
          Config::DataSource::read(config.downstream_auth_username(), true, api), std::string)),
      external_auth_enabled_(config.has_external_auth_provider()),
      external_auth_expiration_enabled_(external_auth_enabled_ &&
                                        config.external_auth_provider().enable_auth_expiration()),
      dns_cache_manager_(cache_manager_factory.get()), dns_cache_(getCache(config)),
      time_source_(time_source), protocol_version_(toCodecRespVersion(config.protocol_version())),
      enable_sharded_publish_(config.enable_sharded_publish()) {

  if (config.settings().enable_redirection() && !config.settings().has_dns_cache_config()) {
    ENVOY_LOG(warn, "redirections without DNS lookups enabled might cause client errors, set the "
                    "dns_cache_config field within the connection pool settings to avoid them");
  }

  auto downstream_auth_password = THROW_OR_RETURN_VALUE(
      Config::DataSource::read(config.downstream_auth_password(), true, api), std::string);
  if (!downstream_auth_password.empty()) {
    downstream_auth_passwords_.emplace_back(downstream_auth_password);
  }

  if (config.downstream_auth_passwords_size() > 0) {
    downstream_auth_passwords_.reserve(downstream_auth_passwords_.size() +
                                       config.downstream_auth_passwords().size());
    for (const auto& source : config.downstream_auth_passwords()) {
      const auto p =
          THROW_OR_RETURN_VALUE(Config::DataSource::read(source, true, api), std::string);
      if (!p.empty()) {
        downstream_auth_passwords_.emplace_back(p);
      }
    }
  }
}

Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr ProxyFilterConfig::getCache(
    const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy& config) {
  if (config.settings().has_dns_cache_config()) {
    auto cache_or_error = dns_cache_manager_->getCache(config.settings().dns_cache_config());
    if (cache_or_error.status().ok()) {
      return cache_or_error.value();
    }
  }
  return nullptr;
}

ProxyStats ProxyFilterConfig::generateStats(const std::string& prefix, Stats::Scope& scope) {
  return {
      ALL_REDIS_PROXY_STATS(POOL_COUNTER_PREFIX(scope, prefix), POOL_GAUGE_PREFIX(scope, prefix))};
}

ProxyFilter::ProxyFilter(Common::Redis::DecoderFactory& factory,
                         Common::Redis::EncoderPtr&& encoder, CommandSplitter::Instance& splitter,
                         ProxyFilterConfigSharedPtr config,
                         ExternalAuth::ExternalAuthClientPtr&& auth_client)
    : decoder_(factory.create(*this)), encoder_(std::move(encoder)), splitter_(splitter),
      config_(config), transaction_(&upstream_transaction_cb_) {
  config_->stats_.downstream_cx_total_.inc();
  config_->stats_.downstream_cx_active_.inc();
  connection_allowed_ = config_->downstream_auth_username_.empty() &&
                        config_->downstream_auth_passwords_.empty() &&
                        !config_->external_auth_enabled_;
  connection_quit_ = false;
  external_auth_call_status_ = ExternalAuthCallStatus::Ready;
  if (auth_client != nullptr) {
    auth_client_ = std::move(auth_client);
  }
}

ProxyFilter::~ProxyFilter() {
  ASSERT(pending_requests_.empty());
  // Sever the subscriber's weak back-ref before our members are destroyed, so a delivery re-entered
  // during teardown drops the push instead of touching a half-destroyed filter.
  if (push_sink_ != nullptr) {
    push_sink_->detach();
  }
  config_->stats_.downstream_cx_active_.dec();
}

void ProxyFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
  callbacks_->connection().addConnectionCallbacks(*this);
  callbacks_->connection().setConnectionStats({config_->stats_.downstream_cx_rx_bytes_total_,
                                               config_->stats_.downstream_cx_rx_bytes_buffered_,
                                               config_->stats_.downstream_cx_tx_bytes_total_,
                                               config_->stats_.downstream_cx_tx_bytes_buffered_,
                                               nullptr, nullptr});
}

void ProxyFilter::onRespValue(Common::Redis::RespValuePtr&& value) {
  // Drop any frame dispatched after the connection has left Open. onData feeds the whole
  // readable buffer to decoder_->decode(), which dispatches EVERY complete frame in a loop. An
  // earlier frame in that same loop can synchronously close the downstream connection — a reply or
  // ack write tripping the slow-subscriber high-watermark (onAboveWriteBufferHighWatermark →
  // closeSlowSubscriber), or a splitter-driven error close — which runs onEvent(LocalClose) inline:
  // it cancels every pending request and resets the subscriber. The decoder loop, however, keeps
  // going. Processing a later frame now would create a fresh PendingRequest + live upstream handle
  // that no future close event will ever cancel (the filter is deferred-deleted with a non-empty
  // queue → ~PendingRequest/handle ASSERT in debug, freed-callback UAF in release), and a
  // SUBSCRIBE would re-create a subscriber on the closed connection via getOrCreateSubscriber
  // (zombie upstream subscription + leaked gauge that no disconnect will reclaim). Bailing
  // here keeps close on the decode boundary, matching the Envoy filter convention.
  if (callbacks_->connection().state() != Network::Connection::State::Open) {
    return;
  }
  pending_requests_.emplace_back(*this);
  PendingRequest& request = pending_requests_.back();

  // If external authentication is enabled and an AUTH command is ongoing,
  // we keep the request in the queue and let it be processed when the
  // authentication response is received.
  if (external_auth_call_status_ == ExternalAuthCallStatus::Pending) {
    request.pending_request_value_ = std::move(value);
    return;
  }

  processRespValue(std::move(value), request);
}

void ProxyFilter::processRespValue(Common::Redis::RespValuePtr&& value, PendingRequest& request) {
  // Pre-HELLO gate on RESP3 listeners: only HELLO / AUTH / QUIT pass until HELLO 3 lands;
  // any other well-formed command array returns -NOPROTO before the splitter. Malformed
  // requests fall through to the splitter's invalid-request path.
  if (config_->protocolVersion() == Common::Redis::RespProtocolVersion::Resp3 &&
      downstream_resp_version_ < 3 && value->type() == Common::Redis::RespType::Array &&
      !value->asArray().empty() &&
      value->asArray()[0].type() == Common::Redis::RespType::BulkString) {
    const std::string cmd = absl::AsciiStrToLower(value->asArray()[0].asString());
    if (cmd != Common::Redis::SupportedCommands::hello() &&
        cmd != Common::Redis::SupportedCommands::auth() &&
        cmd != Common::Redis::SupportedCommands::quit()) {
      // Operational signal: a client sent a data command on a RESP3 listener before completing
      // the HELLO 3 handshake. Counts only this pre-HELLO gate, not HELLO-version mismatches.
      config_->stats_.downstream_rq_noproto_.inc();
      request.onResponse(
          Common::Redis::Utility::makeError(CommandSplitter::Response::get().UnsupportedProtocol));
      return;
    }
  }

  CommandSplitter::SplitRequestPtr split =
      splitter_.makeRequest(std::move(value), request, callbacks_->connection().dispatcher(),
                            callbacks_->connection().streamInfo());
  if (split) {
    // The splitter can immediately respond and destroy the pending request. Only store the handle
    // if the request is still alive.
    request.request_handle_ = std::move(split);
  }
}

DownstreamSubscriberPtr ProxyFilter::getOrCreateSubscriber() {
  // Never CREATE a subscriber on a connection that has left Open (defense-in-depth behind the
  // onRespValue guard): a SUBSCRIBE decoded after a mid-decode-loop close must not install a fresh
  // subscriber + upstream subscription that no disconnect event will ever reclaim (leaked gauge +
  // zombie upstream connection). An already-existing subscriber is still returned (teardown reads
  // it); only creation is gated. The splitter treats a null return as "failed to create subscriber"
  // and surfaces an inline error rather than dereferencing it.
  if (!subscriber_ && callbacks_ &&
      callbacks_->connection().state() == Network::Connection::State::Open) {
    subscriber_ = std::make_shared<DownstreamSubscriber>(
        callbacks_->connection(),
        DownstreamSubscriberStats{config_->stats_.pubsub_push_messages_delivered_,
                                  config_->stats_.pubsub_active_subscriptions_,
                                  config_->stats_.pubsub_subscribe_ack_success_,
                                  config_->stats_.pubsub_subscribe_ack_error_});
    // Route the subscriber's MESSAGE pushes back through this filter so they order behind in-flight
    // replies (FIFO). The subscriber holds the sink weakly; this filter owns the only strong ref.
    if (push_sink_ == nullptr) {
      push_sink_ = std::make_shared<ProxyFilterPushSink>(*this);
    }
    subscriber_->setOrderingSink(push_sink_);
  }
  return subscriber_;
}

void ProxyFilter::onEvent(Network::ConnectionEvent event) {
  // The downstream connection's own event. Runs the full close cleanup, including the subscriber
  // teardown.
  handleConnectionEvent(event, /*is_downstream=*/true);
}

void ProxyFilter::handleConnectionEvent(Network::ConnectionEvent event, bool is_downstream) {
  if (event == Network::ConnectionEvent::Connected) {
    ENVOY_LOG(trace, "new connection to redis proxy filter");
  }
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    ENVOY_LOG(trace, "connection to redis proxy filter closed");
    // The response-FIFO cancel + push-accounting reset below are DOWNSTREAM-close semantics: the
    // client is gone, so cancel everything it had in flight. On an upstream transaction-client
    // close (is_downstream=false — a keyed EXEC/DISCARD closing the transaction client, or the
    // upstream dying mid-transaction) they must NOT run: that client's own in-flight commands are
    // already failed by its ClientImpl::onEvent (registered ahead of this adapter), and the
    // remaining FIFO entries — a deferred SUBSCRIBE awaiting its pool's ack, unrelated pipelined
    // commands on other pools — belong to a live downstream. Cancelling them would strand
    // the downstream with zero responses AND (for the deferred SUBSCRIBE, whose cancel() does not
    // roll back its optimistic registry registration) leave a zombie subscription that later
    // receives pushes it never acked (R8-1).
    if (is_downstream) {
      while (!pending_requests_.empty()) {
        if (pending_requests_.front().request_handle_ != nullptr) {
          pending_requests_.front().request_handle_->cancel();
        }
        pending_requests_.pop_front();
      }
      // Any MESSAGE pushes parked on those requests were destroyed with them; keep the byte
      // accounting invariant (held_push_bytes_ == sum of trailing_pushes_ lengths) intact.
      held_push_bytes_ = 0;
    }
    transaction_.close();

    if (external_auth_call_status_ == ExternalAuthCallStatus::Pending) {
      auth_client_->cancel();
    }

    // The subscriber teardown below is DOWNSTREAM-only. ProxyFilter is also the connection callback
    // of the transaction client's UPSTREAM connection: that client is created with
    // transaction_.connection_cb_, which is the upstream_transaction_cb_ adapter (see the ctor and
    // header), so a keyed EXEC/DISCARD that closes the transaction client (NoFlush -> synchronous
    // LocalClose), or an upstream that dies mid-transaction, routes here with is_downstream=false.
    // Those must NOT destroy the live downstream subscriber — real Redis keeps subscriptions across
    // MULTI/EXEC; tearing them down would silently strand the client (open downstream, lost
    // subscription, no error). Only transaction_.close() and the auth cancel above run on both
    // close directions; the response-FIFO cancel (R8-1) and the subscriber teardown below are
    // downstream-only.
    if (!is_downstream) {
      return;
    }

    // Clean up subscriptions on disconnect — notify registry to send upstream UNSUBSCRIBE.
    if (subscriber_) {
      uint64_t total_removed = subscriber_->subscribedChannels().size();
      // Snapshot via std::exchange and iterate the snapshot. With today's removeSubscriber()
      // this is unnecessary (no path mutates subscription_registries_ during iteration), but
      // the cost is one move and zero allocations, and it makes the loop safe against any
      // future reentrant change that mutates the vector via a subscriber-side callback.
      // Mirrors the snapshot pattern in SubscriptionRegistry::clear(). Also subsumes the
      // explicit .clear() that used to live below the loop.
      auto registries_snapshot = std::exchange(subscription_registries_, {});
      for (auto& weak_registry : registries_snapshot) {
        if (auto registry = weak_registry.lock()) {
          registry->removeSubscriber(subscriber_);
        }
      }
      // Count the disconnect as an unsubscribe of every channel the subscriber still held. The
      // active-subscriptions GAUGE is NOT decremented here: removeSubscriber above dropped
      // each channel through removeChannel (which decrements it), and any channels stranded by a
      // concurrent cluster-removal clear() are drained by ~DownstreamSubscriber. A subtract here
      // would double-count.
      if (total_removed > 0) {
        config_->stats_.pubsub_unsubscribe_total_.add(total_removed);
      }
    }
    subscriber_.reset();
  }
}

void ProxyFilter::onQuit(PendingRequest& request) {
  auto response = std::make_unique<Common::Redis::RespValue>();
  response->type(Common::Redis::RespType::SimpleString);
  response->asString() = "OK";
  connection_quit_ = true;
  request.onResponse(std::move(response));
}

bool ProxyFilter::connectionAllowed() {
  // Check for external auth expiration.
  if (connection_allowed_ && config_->external_auth_expiration_enabled_) {
    const auto now_epoch = config_->timeSource().systemTime().time_since_epoch().count();
    if (now_epoch > external_auth_expiration_epoch_) {
      ENVOY_LOG(info, "Redis external authentication expired. Disallowing further commands.");
      connection_allowed_ = false;
    }
  }

  return connection_allowed_;
}

void ProxyFilter::onAuthenticateExternal(CommandSplitter::SplitCallbacks& request,
                                         ExternalAuth::AuthenticateResponsePtr&& response) {
  // The deferred HELLO version (set when an inline ``HELLO N AUTH ...`` was routed to external
  // auth) distinguishes the AUTH-command path (emit +OK / error) from the HELLO N AUTH ...
  // path (emit HELLO Map / error). Consume it through the callback interface rather than
  // downcasting to PendingRequest.
  const std::optional<uint32_t> hello_auth_version = request.takePendingHelloAuthVersion();
  const bool is_hello_auth = hello_auth_version.has_value();

  Common::Redis::RespValuePtr redis_response;
  const bool authorized = response->status == ExternalAuth::AuthenticationRequestStatus::Authorized;
  const bool unauthorized =
      response->status == ExternalAuth::AuthenticationRequestStatus::Unauthorized;

  if (authorized) {
    connection_allowed_ = true;
    if (config_->external_auth_expiration_enabled_) {
      external_auth_expiration_epoch_ = response->expiration.seconds() * 1000000;
    }
    if (is_hello_auth) {
      // Flip downstream version BEFORE emitting the HELLO Map so the reply (and pipelined
      // followers) encode in the negotiated version — see PendingRequest::setDownstreamRespVersion.
      const uint32_t version = *hello_auth_version;
      request.setDownstreamRespVersion(version);
      redis_response = CommandSplitter::buildHelloReply(version);
    } else {
      redis_response = std::make_unique<Common::Redis::RespValue>();
      redis_response->type(Common::Redis::RespType::SimpleString);
      redis_response->asString() = "OK";
    }
  } else if (unauthorized) {
    connection_allowed_ = false;
    redis_response = std::make_unique<Common::Redis::RespValue>();
    redis_response->type(Common::Redis::RespType::Error);
    if (is_hello_auth) {
      // Match the WRONGPASS shape the splitter emits for a denied local-auth HELLO so RESP3
      // clients see the same error code regardless of which auth backend is configured. The
      // provider-supplied detail is sanitized: a CR/LF in it would re-frame the RESP error line.
      const std::string detail =
          response->message.empty() ? "invalid username-password pair" : response->message;
      redis_response->asString() =
          fmt::format("WRONGPASS {}", Common::Redis::sanitizeControlBytes(detail));
    } else {
      const std::string detail = response->message.empty() ? "unauthorized" : response->message;
      redis_response->asString() =
          fmt::format("ERR {}", Common::Redis::sanitizeControlBytes(detail));
    }
  } else {
    redis_response = std::make_unique<Common::Redis::RespValue>();
    redis_response->type(Common::Redis::RespType::Error);
    redis_response->asString() = "ERR external authentication failed";
    ENVOY_LOG(error, "Redis external authentication failed: {}", response->message);
  }

  external_auth_call_status_ = ExternalAuthCallStatus::Ready;

  request.onResponse(std::move(redis_response));

  // Resume any commands the client pipelined behind the auth request while we were waiting on
  // the external provider.
  resumeAuthHeldRequests();
}

void ProxyFilter::resumeAuthHeldRequests() {
  // Replay every held entry in FIFO order, not just the front: a held command can sit behind
  // a still-in-flight upstream request. No iterator survives a processRespValue call: the
  // onResponse flush loop pops every consecutive completed front entry (possibly several), and
  // a resumed AUTH whose external-auth call resolves synchronously (gRPC send() can fail
  // inline) re-enters this method and drains further entries. Re-scanning from begin() after
  // every dispatch is therefore required for memory safety; the scan is bounded by how many
  // commands the client pipelined during one auth round trip. The reentrant guard keeps the
  // outermost invocation as the single drain loop; the nested call returns immediately and the
  // outer loop re-checks the auth status on its next pass.
  if (resuming_held_requests_) {
    return;
  }
  resuming_held_requests_ = true;
  Cleanup reset_resuming_flag([this]() { resuming_held_requests_ = false; });
  while (external_auth_call_status_ != ExternalAuthCallStatus::Pending) {
    auto it = std::find_if(
        pending_requests_.begin(), pending_requests_.end(),
        [](const PendingRequest& entry) { return entry.pending_request_value_ != nullptr; });
    if (it == pending_requests_.end()) {
      break;
    }
    PendingRequest& held = *it;
    // Detach the value into a local FIRST so this entry is unconditionally cleared before the
    // next find_if pass. processRespValue does not always consume its argument (the
    // ``NOPROTO`` gate drops it; the split-request handle path leaves it untouched), and since
    // the scan restarts from begin() each pass, an entry left non-null would be re-selected
    // forever.
    Common::Redis::RespValuePtr value = std::move(held.pending_request_value_);
    processRespValue(std::move(value), held);
  }
}

void ProxyFilter::onAuth(PendingRequest& request, const std::string& password) {
  if (config_->external_auth_enabled_) {
    external_auth_call_status_ = ExternalAuthCallStatus::Pending;
    auth_client_->authenticateExternal(*this, request, callbacks_->connection().streamInfo(),
                                       EMPTY_STRING, password);
    return;
  }

  Common::Redis::RespValuePtr response;
  if (config_->downstream_auth_passwords_.empty()) {
    response = Common::Redis::Utility::makeError("ERR Client sent AUTH, but no password is set");
  } else if (checkPassword(password)) {
    response = std::make_unique<Common::Redis::RespValue>();
    response->type(Common::Redis::RespType::SimpleString);
    response->asString() = "OK";
    connection_allowed_ = true;
  } else {
    // A failed AUTH must not revoke an existing grant: real Redis leaves the connection's
    // authentication state unchanged on an AUTH error, replying only with the error. Leave
    // connection_allowed_ as-is rather than revoking auth for an authenticated client, which
    // would otherwise lock every subsequent command (including UNSUBSCRIBE) behind NOAUTH (R8-8).
    response = Common::Redis::Utility::makeError("ERR invalid password");
  }
  request.onResponse(std::move(response));
}

void ProxyFilter::onAuth(PendingRequest& request, const std::string& username,
                         const std::string& password) {
  if (config_->external_auth_enabled_) {
    // Set Pending before dispatch so a synchronously-resolving callback doesn't get the
    // status flipped back to Pending after it ran.
    external_auth_call_status_ = ExternalAuthCallStatus::Pending;
    auth_client_->authenticateExternal(*this, request, callbacks_->connection().streamInfo(),
                                       username, password);
    return;
  }

  Common::Redis::RespValuePtr response;
  if (config_->downstream_auth_username_.empty() && config_->downstream_auth_passwords_.empty()) {
    response = Common::Redis::Utility::makeError(
        "ERR Client sent AUTH, but no username-password pair is set");
  } else if (checkCredentials(username, password)) {
    response = std::make_unique<Common::Redis::RespValue>();
    response->type(Common::Redis::RespType::SimpleString);
    response->asString() = "OK";
    connection_allowed_ = true;
  } else {
    // A failed AUTH must not revoke an existing grant — see the R8-8 note in the single-argument
    // onAuth above. Reply with the error but leave connection_allowed_ unchanged.
    response = Common::Redis::Utility::makeError("WRONGPASS invalid username-password pair");
  }
  request.onResponse(std::move(response));
}

CommandSplitter::SplitCallbacks::AuthAttempt
ProxyFilter::attemptDownstreamAuthInline(PendingRequest& request, const std::string& username,
                                         const std::string& password, uint32_t requested_version) {
  using AuthAttempt = CommandSplitter::SplitCallbacks::AuthAttempt;
  // External auth is async (gRPC round trip via authenticateExternal). Stash the requested
  // protocol version on the PendingRequest and start the round trip; onAuthenticateExternal
  // will emit the deferred HELLO Map (success) or error (failure) for this version when the
  // round trip completes. external_auth_call_status_ = Pending makes any subsequent
  // pipelined request decoded in the same onData() pass land in pending_request_value_ until
  // the round trip resolves — same flow that already gates a separate AUTH command.
  if (config_->external_auth_enabled_) {
    request.pending_hello_auth_version_ = requested_version;
    external_auth_call_status_ = ExternalAuthCallStatus::Pending;
    auth_client_->authenticateExternal(*this, request, callbacks_->connection().streamInfo(),
                                       username, password);
    return AuthAttempt::ImplOwnsResponse;
  }
  // No downstream credentials configured: emit the same error the username+password AUTH path
  // returns (``onAuth(username, password)``) rather than ``WRONGPASS``. ``HELLO N AUTH`` always
  // carries a username and password, so it mirrors the two-arg form's "no username-password
  // pair is set" message. Emitting directly and returning ImplOwnsResponse keeps the splitter from
  // also emitting (the impl owns the response).
  if (config_->downstream_auth_passwords_.empty() && config_->downstream_auth_username_.empty()) {
    request.onResponse(Common::Redis::Utility::makeError(
        "ERR Client sent AUTH, but no username-password pair is set"));
    return AuthAttempt::ImplOwnsResponse;
  }
  // Same local-credential policy as the AUTH command paths (checkCredentials). A failed re-AUTH
  // must not revoke an existing grant (see the R8-8 note in onAuth): set connection_allowed_ only
  // on success, and report Denied for this attempt without clearing a prior successful AUTH.
  const bool credentials_ok = checkCredentials(username, password);
  if (credentials_ok) {
    connection_allowed_ = true;
  }
  return credentials_ok ? AuthAttempt::Allowed : AuthAttempt::Denied;
}

bool ProxyFilter::checkCredentials(const std::string& username, const std::string& password) {
  // Single policy for both auth entry points (``AUTH`` and ``HELLO N AUTH``):
  //  - no configured username: the default user may be named "default" (Redis 6 ACL synonym)
  //  or left empty; only the password is checked.
  //  - configured username: exact match.
  if (config_->downstream_auth_username_.empty()) {
    return (username.empty() || username == "default") && checkPassword(password);
  }
  return username == config_->downstream_auth_username_ && checkPassword(password);
}

bool ProxyFilter::checkPassword(const std::string& password) {
  for (const auto& p : config_->downstream_auth_passwords_) {
    if (password.length() == p.length() &&
        CRYPTO_memcmp(password.data(), p.data(), p.length()) == 0) {
      return true;
    }
  }
  return false;
}

void ProxyFilter::respond(PendingRequest& request, CommandSplitter::RespValueFrames&& frames) {
  ASSERT(!pending_requests_.empty());
  // Splitter contract: respond() is the one-shot terminal. A second respond() would be a
  // use-after-mark — the FIFO entry may already have been popped and destroyed by the flush the
  // first respond() triggered.
  ASSERT(!request.complete_);
  // Move the splitter's accumulated frames into the FIFO entry in arrival order. Zero frames is
  // normal for a request that completed with nothing to write (a bare UNSUBSCRIBE on an
  // already-gone subscriber); one is the common single-reply case (via the onResponse sugar);
  // several is a multi-channel SUBSCRIBE / UNSUBSCRIBE whose per-channel acks and inline -ERR
  // replies share this entry. Move-assign the whole vector (respond() is the one-shot terminal, so
  // pending_responses_ is empty here): steals the splitter's buffer instead of allocating a
  // container node per frame (the previous std::list added a malloc to every response on the hot
  // path).
  request.pending_responses_ = std::move(frames);
  request.complete_ = true;
  request.request_handle_ = nullptr;
  // May pop *request from the FIFO and destroy it — do not touch request after this.
  flushReadyResponses();
}

void ProxyFilter::flushReadyResponses() {
  // Drain only the contiguously-complete prefix from the front of pending_requests_. A complete
  // entry behind an incomplete one stays queued — the SUBSCRIBE-behind-GET case where the
  // SUBSCRIBE error must wait for the GET reply to land first.
  while (!pending_requests_.empty() && pending_requests_.front().complete_) {
    auto& front = pending_requests_.front();
    auto request_version = front.resp_version_at_creation_;
    auto current_version = downstream_resp_version_;
    if (request_version != current_version) {
      encoder_->setProtocolVersion(Common::Redis::toRespProtocolVersion(request_version));
    }
    // Drain all frames the splitter accumulated for this request, in arrival order. Empty deque is
    // normal for an entry that completed with nothing to write (a bare UNSUBSCRIBE on an
    // already-gone subscriber); a fresh SUBSCRIBE instead fills its ack frames here as each
    // channel's upstream ack lands, then flushes them all once complete.
    for (auto& resp : front.pending_responses_) {
      encoder_->encode(*resp, encoder_buffer_);
    }
    if (request_version != current_version) {
      encoder_->setProtocolVersion(Common::Redis::toRespProtocolVersion(current_version));
    }
    // Flush any MESSAGE pushes parked behind this request right after its reply bytes, preserving
    // FIFO order. They are already RESP3-encoded, so they append verbatim regardless of the
    // per-request version switch above.
    if (front.trailing_pushes_ != nullptr && front.trailing_pushes_->length() > 0) {
      held_push_bytes_ -= front.trailing_pushes_->length();
      encoder_buffer_.move(*front.trailing_pushes_);
      // The parked frames are now on the wire: count them as delivered here, so frames that
      // were instead dropped on eviction/disconnect (never reaching this flush) are not counted.
      config_->stats_.pubsub_push_messages_delivered_.add(front.trailing_push_count_);
    }
    pending_requests_.pop_front();
  }

  if (encoder_buffer_.length() > 0) {
    callbacks_->connection().write(encoder_buffer_, false);
  }

  if (pending_requests_.empty() && connection_quit_) {
    callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
    connection_quit_ = false;
    return;
  }

  // Check for drain close only if there are no pending responses.
  if (pending_requests_.empty() &&
      config_->drain_decision_.drainClose(Network::DrainDirection::All) &&
      config_->runtime_.snapshot().featureEnabled(config_->redis_drain_close_runtime_key_, 100)) {
    config_->stats_.downstream_cx_drain_close_.inc();
    callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
  }

  // Check if there is an active transaction that needs to be closed.
  if (transaction_.should_close_ && pending_requests_.empty()) {
    transaction_.close();
  }
}

void ProxyFilter::enqueueOrderedPush(Buffer::Instance& encoded) {
  const uint64_t push_bytes = encoded.length();
  if (push_bytes == 0) {
    return;
  }
  // In-order fast path: nothing is queued ahead, so the push goes straight to the wire in the order
  // it arrived — delivered now, so count it (the message counter reflects ACTUAL delivery, not
  // enqueue, so evicted/dropped parked frames are not double-counted). write() drains ``encoded``.
  if (pending_requests_.empty()) {
    // Slow-subscriber eviction on this path cannot rely on the connection high-watermark callback
    // (onAboveWriteBufferHighWatermark) alone: that callback is EDGE-triggered — it fires once when
    // the write buffer crosses the high mark and re-arms only after a drain below the low mark. If
    // its rising edge was consumed earlier while the eviction gate was false (e.g. a pre-SUBSCRIBE
    // ordinary-reply backlog crossed the mark while ``subscriber_`` was still null), the latch
    // stays set and the callback never re-fires — so an unread subscriber's fast-path push burst
    // would grow the connection write buffer without bound (memory-exhaustion DoS). Add a LEVEL
    // check here, BEFORE the write (a post-write check is unsafe — write() may synchronously close
    // and destroy this filter): if the buffer is already over the high mark for an active
    // subscriber, evict now instead of appending more. This bounds the fast-path push contribution
    // to the write buffer at the high watermark plus at most one push (the push that first crosses
    // is written; the next one evicts) — the same bound the park path applies to
    // ``held_push_bytes_``. Ordinary pipelined replies still use the write buffer as they do on the
    // mainline proxy path; this level check governs only the subscriber push fast path.
    if (subscriber_ != nullptr && subscriber_->totalSubscriptionCount() > 0 &&
        !slow_subscriber_closed_ && callbacks_->connection().aboveHighWatermark()) {
      // Drop the undelivered frame (honor routePush's empty-buffer contract) before evicting; the
      // subscriber is closing, so there is nothing to deliver it to.
      encoded.drain(encoded.length());
      closeSlowSubscriber("write buffer above high watermark on fast-path push");
      return;
    }
    // Count BEFORE the write: write() can in principle synchronously drive a downstream close that
    // re-enters and destroys this filter, after which touching ``config_`` would be a use-after-
    // free. Counting first is still delivery-accurate (write() hands the frame to the connection
    // buffer) and mirrors the park path below, which mutates its counters before its own close.
    config_->stats_.pubsub_push_messages_delivered_.inc();
    callbacks_->connection().write(encoded, false);
    return;
  }
  // Ordinary replies are still in flight. Park the push on the most recently queued request so it
  // flushes at its correct FIFO position: after every request that preceded it, and without waiting
  // on requests that arrive after it (each request releases its own parked pushes in
  // flushReadyResponses). Count it there, at flush — not here — so a parked frame dropped on
  // eviction/disconnect is not counted as delivered. move() transfers the slices out of
  // ``encoded`` (no copy), leaving it empty.
  PendingRequest& back = pending_requests_.back();
  if (back.trailing_pushes_ == nullptr) {
    back.trailing_pushes_ = std::make_unique<Buffer::OwnedImpl>(); // allocate only on first park
  }
  back.trailing_pushes_->move(encoded);
  ++back.trailing_push_count_;
  held_push_bytes_ += push_bytes;
  // The parked bytes live in this app buffer, not the connection write buffer, so the connection
  // high-watermark (the slow-subscriber path in onAboveWriteBufferHighWatermark) cannot observe
  // them. Bound them by the same per-connection limit so a subscriber whose FIFO never drains is
  // evicted instead of buffering without limit. A zero limit means "unlimited" and disables the
  // bound.
  const uint32_t limit = callbacks_->connection().bufferLimit();
  if (limit > 0 && held_push_bytes_ > limit && !slow_subscriber_closed_) {
    closeSlowSubscriber(
        fmt::format("parked push bytes {} above limit {}", held_push_bytes_, limit));
  }
}

void ProxyFilter::closeSlowSubscriber(const std::string& reason) {
  slow_subscriber_closed_ = true;
  config_->stats_.pubsub_slow_subscriber_closed_.inc();
  ENVOY_LOG(debug, "redis: closing slow pub/sub subscriber ({})", reason);
  callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
}

Network::FilterStatus ProxyFilter::onData(Buffer::Instance& data, bool) {
  TRY_NEEDS_AUDIT {
    decoder_->decode(data);
    return Network::FilterStatus::Continue;
  }
  END_TRY catch (Common::Redis::ProtocolError&) {
    config_->stats_.downstream_cx_protocol_error_.inc();
    Common::Redis::RespValue error;
    error.type(Common::Redis::RespType::Error);
    error.asString() = "downstream protocol error";
    encoder_->encode(error, encoder_buffer_);
    callbacks_->connection().write(encoder_buffer_, false);
    callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    return Network::FilterStatus::StopIteration;
  }
}

ProxyFilter::PendingRequest::PendingRequest(ProxyFilter& parent)
    : resp_version_at_creation_(parent.downstream_resp_version_), parent_(parent) {
  parent.config_->stats_.downstream_rq_total_.inc();
  parent.config_->stats_.downstream_rq_active_.inc();
}

ProxyFilter::PendingRequest::~PendingRequest() {
  parent_.config_->stats_.downstream_rq_active_.dec();
}

void ProxyFilter::PendingRequest::setDownstreamRespVersion(uint32_t version) {
  // A version flip must never happen while the connection holds active subscriptions: pub/sub Push
  // frames are RESP3-only and the fan-out assumes a fixed RESP3 downstream, so flipping the encoder
  // to RESP2 mid-subscription (or back) would corrupt the stream. Today this is unreachable: the
  // pre-HELLO NOPROTO gate only accepts a HELLO whose version equals the listener policy, so a
  // subscribed RESP3 client can only re-assert HELLO 3 (a no-op flip). Assert it so a future
  // "accept both 2 and 3" mode cannot silently introduce the corruption (R8-N3).
  ASSERT(parent_.subscriber_ == nullptr || parent_.subscriber_->totalSubscriptionCount() == 0 ||
         parent_.downstream_resp_version_ == version);
  parent_.downstream_resp_version_ = version;
  parent_.encoder_->setProtocolVersion(Common::Redis::toRespProtocolVersion(version));
  // Re-stamp this HELLO and any auth-held followers; onResponse would otherwise encode
  // their replies in the pre-HELLO version.
  resp_version_at_creation_ = version;
  bool past_this = false;
  for (auto& queued : parent_.pending_requests_) {
    if (&queued == this) {
      past_this = true;
      continue;
    }
    if (past_this && queued.pending_request_value_) {
      queued.resp_version_at_creation_ = version;
    }
  }
}

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
