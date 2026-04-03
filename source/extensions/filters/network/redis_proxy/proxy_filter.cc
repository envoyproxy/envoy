#include "source/extensions/filters/network/redis_proxy/proxy_filter.h"

#include <algorithm>
#include <cstdint>
#include <string>

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/config/datasource.h"
#include "source/common/config/utility.h"
#include "source/extensions/filters/network/redis_proxy/config.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

ProxyFilterConfig::ProxyFilterConfig(
    const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy& config,
    Stats::Scope& scope, const Network::DrainDecision& drain_decision, Runtime::Loader& runtime,
    Api::Api& api, TimeSource& time_source,
    Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory& cache_manager_factory,
    Upstream::ClusterManager& cluster_manager, std::vector<std::string> response_bearing_clusters)
    : drain_decision_(drain_decision), runtime_(runtime),
      stat_prefix_(fmt::format("redis.{}.", config.stat_prefix())),
      stats_(generateStats(stat_prefix_, scope)),
      downstream_auth_username_(THROW_OR_RETURN_VALUE(
          Config::DataSource::read(config.downstream_auth_username(), true, api), std::string)),
      external_auth_enabled_(config.has_external_auth_provider()),
      external_auth_expiration_enabled_(external_auth_enabled_ &&
                                        config.external_auth_provider().enable_auth_expiration()),
      dns_cache_manager_(cache_manager_factory.get()), dns_cache_(getCache(config)),
      time_source_(time_source), cluster_manager_(cluster_manager),
      response_bearing_clusters_(std::move(response_bearing_clusters)) {

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

uint32_t ProxyFilterConfig::clusterRespVersion() const {
  // Floor across response-bearing clusters, evaluated against the worker's
  // live thread-local cluster view. Missing/unknown clusters cap at RESP2 —
  // the conservative default that lets HELLO 3 be rejected with -NOPROTO
  // until the cluster lands. The function is invoked on the HELLO command
  // path (once per downstream connection) and the per-command RESP-cap
  // guard in CommandSplitter::makeRequest (once per non-HELLO command); both
  // run on the worker thread.
  uint32_t cap = 3;
  bool any_seen = false;
  for (const auto& name : response_bearing_clusters_) {
    auto* tlc = cluster_manager_.getThreadLocalCluster(name);
    uint32_t v = 2;
    if (tlc != nullptr) {
      v = (ProtocolOptionsConfigImpl::upstreamProtocolVersion(tlc->info()) ==
           envoy::extensions::filters::network::redis_proxy::v3::RedisProtocolOptions::
               UpstreamProtocol::RESP3)
              ? 3
              : 2;
    }
    cap = std::min(cap, v);
    any_seen = true;
  }
  return any_seen ? cap : 2;
}

ProxyFilter::ProxyFilter(Common::Redis::DecoderFactory& factory,
                         Common::Redis::EncoderPtr&& encoder, CommandSplitter::Instance& splitter,
                         ProxyFilterConfigSharedPtr config,
                         ExternalAuth::ExternalAuthClientPtr&& auth_client)
    : decoder_(factory.create(*this)), encoder_(std::move(encoder)), splitter_(splitter),
      config_(config), transaction_(this) {
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
  CommandSplitter::SplitRequestPtr split =
      splitter_.makeRequest(std::move(value), request, callbacks_->connection().dispatcher(),
                            callbacks_->connection().streamInfo());
  if (split) {
    // The splitter can immediately respond and destroy the pending request. Only store the handle
    // if the request is still alive.
    request.request_handle_ = std::move(split);
  }
}

void ProxyFilter::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::Connected) {
    ENVOY_LOG(trace, "new connection to redis proxy filter");
  }
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    ENVOY_LOG(trace, "connection to redis proxy filter closed");
    while (!pending_requests_.empty()) {
      if (pending_requests_.front().request_handle_ != nullptr) {
        pending_requests_.front().request_handle_->cancel();
      }
      pending_requests_.pop_front();
    }
    transaction_.close();

    if (external_auth_call_status_ == ExternalAuthCallStatus::Pending) {
      auth_client_->cancel();
    }
  }
}

void ProxyFilter::onQuit(PendingRequest& request) {
  Common::Redis::RespValuePtr response{new Common::Redis::RespValue()};
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
  // The ExternalAuth::AuthenticateCallback interface types ``request`` as SplitCallbacks for
  // ABI generality, but the only producer of these calls is ProxyFilter::onAuth /
  // attemptDownstreamAuthInline, both of which pass a PendingRequest. Downcast so we can
  // observe pending_hello_auth_version_, which distinguishes the AUTH-command path (emit
  // +OK / error) from the HELLO N AUTH ... path (emit HELLO Map / error).
  auto& pending = static_cast<PendingRequest&>(request);
  const bool is_hello_auth = pending.pending_hello_auth_version_.has_value();

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
      // Flip the negotiated downstream RESP version BEFORE emitting the HELLO Map so the
      // encoder serializes the Map natively (RESP3) or down-converts to a flat array (RESP2)
      // matching what the client just negotiated. setDownstreamRespVersion also updates the
      // stamp on the in-flight PendingRequest so ProxyFilter::onResponse does not flip the
      // encoder back to the pre-HELLO version when sending this very reply.
      const uint32_t version = *pending.pending_hello_auth_version_;
      pending.setDownstreamRespVersion(version);
      redis_response = CommandSplitter::buildHelloReply(version);

      // Re-stamp queued pending_request_value_ entries that landed BEHIND this HELLO request
      // while external_auth_call_status_ was Pending. They were created with
      // resp_version_at_creation_ = the pre-HELLO downstream version (typically RESP2);
      // without this re-stamp, ProxyFilter::onResponse's stamp-vs-current branch would
      // setProtocolVersion(Resp2) → encode → setProtocolVersion(Resp3) for each one, sending
      // RESP2 reply shapes on a freshly-negotiated RESP3 connection. The contract HELLO N
      // AUTH establishes is "from this command forward, the connection speaks RESP N", so any
      // command pipelined after the HELLO inherits the new version regardless of when the
      // client put it on the wire.
      bool past_hello = false;
      for (auto& queued : pending_requests_) {
        if (&queued == &pending) {
          past_hello = true;
          continue;
        }
        if (past_hello && queued.pending_request_value_) {
          queued.resp_version_at_creation_ = version;
        }
      }
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
      // clients see the same error code regardless of which auth backend is configured.
      const std::string detail =
          response->message.empty() ? "invalid username-password pair" : response->message;
      redis_response->asString() = fmt::format("WRONGPASS {}", detail);
    } else {
      const std::string detail = response->message.empty() ? "unauthorized" : response->message;
      redis_response->asString() = fmt::format("ERR {}", detail);
    }
  } else {
    redis_response = std::make_unique<Common::Redis::RespValue>();
    redis_response->type(Common::Redis::RespType::Error);
    redis_response->asString() = "ERR external authentication failed";
    ENVOY_LOG(error, "Redis external authentication failed: {}", response->message);
  }

  external_auth_call_status_ = ExternalAuthCallStatus::Ready;
  pending.pending_hello_auth_version_.reset();

  request.onResponse(std::move(redis_response));

  // Resume any commands the client pipelined behind the auth request while we were waiting on
  // the external provider.
  resumeAuthHeldRequests();
}

void ProxyFilter::resumeAuthHeldRequests() {
  // Iterate the full pending_requests_ list in FIFO order — the front-only loop this replaces
  // missed every held entry that sat behind a still-in-flight upstream request (e.g. a GET
  // outstanding before the AUTH/HELLO entry, or a held entry after a sibling that just went
  // upstream from an earlier resume), so those held entries hung forever waiting on a resume
  // that never came. std::list iterators are stable across non-self pops, so advancing `it`
  // before processing protects against the synchronously-popped-self case in processRespValue
  // (splitter callback emits onResponse → front-pop loop pops the entry). External-auth status
  // is re-checked each iteration: if a resumed held entry is itself an AUTH or HELLO N AUTH
  // and its splitter dispatch starts a new round trip, leave the rest of the list held until
  // that second round trip completes (which will re-enter resumeAuthHeldRequests via
  // onAuthenticateExternal).
  for (auto it = pending_requests_.begin(); it != pending_requests_.end();) {
    if (external_auth_call_status_ == ExternalAuthCallStatus::Pending) {
      return;
    }
    if (!it->pending_request_value_) {
      ++it;
      continue;
    }
    PendingRequest& held = *it++;
    processRespValue(std::move(held.pending_request_value_), held);
  }
}

void ProxyFilter::onAuth(PendingRequest& request, const std::string& password) {
  if (config_->external_auth_enabled_) {
    external_auth_call_status_ = ExternalAuthCallStatus::Pending;
    auth_client_->authenticateExternal(*this, request, callbacks_->connection().streamInfo(),
                                       EMPTY_STRING, password);
    return;
  }

  Common::Redis::RespValuePtr response{new Common::Redis::RespValue()};
  if (config_->downstream_auth_passwords_.empty()) {
    response->type(Common::Redis::RespType::Error);
    response->asString() = "ERR Client sent AUTH, but no password is set";
  } else if (checkPassword(password)) {
    response->type(Common::Redis::RespType::SimpleString);
    response->asString() = "OK";
    connection_allowed_ = true;
  } else {
    response->type(Common::Redis::RespType::Error);
    response->asString() = "ERR invalid password";
    connection_allowed_ = false;
  }
  request.onResponse(std::move(response));
}

void ProxyFilter::onAuth(PendingRequest& request, const std::string& username,
                         const std::string& password) {
  if (config_->external_auth_enabled_) {
    auth_client_->authenticateExternal(*this, request, callbacks_->connection().streamInfo(),
                                       username, password);
    external_auth_call_status_ = ExternalAuthCallStatus::Pending;
    return;
  }

  Common::Redis::RespValuePtr response{new Common::Redis::RespValue()};
  if (config_->downstream_auth_username_.empty() && config_->downstream_auth_passwords_.empty()) {
    response->type(Common::Redis::RespType::Error);
    response->asString() = "ERR Client sent AUTH, but no username-password pair is set";
  } else if (config_->downstream_auth_username_.empty() && username == "default" &&
             checkPassword(password)) {
    // empty username and "default" are synonymous in Redis 6 ACLs
    response->type(Common::Redis::RespType::SimpleString);
    response->asString() = "OK";
    connection_allowed_ = true;
  } else if (username == config_->downstream_auth_username_ && checkPassword(password)) {
    response->type(Common::Redis::RespType::SimpleString);
    response->asString() = "OK";
    connection_allowed_ = true;
  } else {
    response->type(Common::Redis::RespType::Error);
    response->asString() = "WRONGPASS invalid username-password pair";
    connection_allowed_ = false;
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
    return AuthAttempt::Pending;
  }
  // No downstream credentials configured: any HELLO AUTH attempt is denied.
  // Matches the AUTH-command behavior in onAuth() ("Client sent AUTH, but
  // no password is set").
  if (config_->downstream_auth_passwords_.empty() && config_->downstream_auth_username_.empty()) {
    return AuthAttempt::Denied;
  }
  // Local credential check, mirroring the two onAuth overloads:
  //   - Empty configured username + "default" supplied (Redis 6 ACL synonym).
  //   - Configured username matches.
  // When no username was configured but credentials were, only the password
  // path applies; a username supplied alongside is rejected unless it is
  // "default".
  bool credentials_match = false;
  if (config_->downstream_auth_username_.empty()) {
    credentials_match = (username.empty() || username == "default") && checkPassword(password);
  } else {
    credentials_match = (username == config_->downstream_auth_username_) && checkPassword(password);
  }
  connection_allowed_ = credentials_match;
  return credentials_match ? AuthAttempt::Allowed : AuthAttempt::Denied;
}

bool ProxyFilter::checkPassword(const std::string& password) {
  for (const auto& p : config_->downstream_auth_passwords_) {
    if (password == p) {
      return true;
    }
  }
  return false;
}

void ProxyFilter::onResponse(PendingRequest& request, Common::Redis::RespValuePtr&& value) {
  ASSERT(!pending_requests_.empty());
  request.pending_response_ = std::move(value);
  request.request_handle_ = nullptr;

  // The response we got might not be in order, so flush out what we can. (A new response may
  // unlock several out of order responses).
  while (!pending_requests_.empty() && pending_requests_.front().pending_response_) {
    auto& front = pending_requests_.front();
    auto request_version = front.resp_version_at_creation_;
    auto current_version = downstream_resp_version_;
    if (request_version != current_version) {
      encoder_->setProtocolVersion(Common::Redis::toRespProtocolVersion(request_version));
    }
    encoder_->encode(*front.pending_response_, encoder_buffer_);
    if (request_version != current_version) {
      encoder_->setProtocolVersion(Common::Redis::toRespProtocolVersion(current_version));
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

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
