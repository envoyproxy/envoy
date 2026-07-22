#include "source/extensions/filters/network/redis_proxy/proxy_filter.h"

#include <openssl/mem.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>

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
      time_source_(time_source), protocol_version_(toCodecRespVersion(config.protocol_version())) {

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

  auto response = std::make_unique<Common::Redis::RespValue>();
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
    // Set Pending before dispatch so a synchronously-resolving callback doesn't get the
    // status flipped back to Pending after it ran.
    external_auth_call_status_ = ExternalAuthCallStatus::Pending;
    auth_client_->authenticateExternal(*this, request, callbacks_->connection().streamInfo(),
                                       username, password);
    return;
  }

  auto response = std::make_unique<Common::Redis::RespValue>();
  if (config_->downstream_auth_username_.empty() && config_->downstream_auth_passwords_.empty()) {
    response->type(Common::Redis::RespType::Error);
    response->asString() = "ERR Client sent AUTH, but no username-password pair is set";
  } else if (checkCredentials(username, password)) {
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
    return AuthAttempt::ImplOwnsResponse;
  }
  // No downstream credentials configured: emit the same error the username+password AUTH path
  // returns (``onAuth(username, password)``) rather than ``WRONGPASS``. ``HELLO N AUTH`` always
  // carries a username and password, so it mirrors the two-arg form's "no username-password
  // pair is set" message. Emitting directly and returning ImplOwnsResponse keeps the splitter from
  // also emitting (the impl owns the response).
  if (config_->downstream_auth_passwords_.empty() && config_->downstream_auth_username_.empty()) {
    auto response = std::make_unique<Common::Redis::RespValue>();
    response->type(Common::Redis::RespType::Error);
    response->asString() = "ERR Client sent AUTH, but no username-password pair is set";
    request.onResponse(std::move(response));
    return AuthAttempt::ImplOwnsResponse;
  }
  // Same local-credential policy as the AUTH command paths (checkCredentials).
  connection_allowed_ = checkCredentials(username, password);
  return connection_allowed_ ? AuthAttempt::Allowed : AuthAttempt::Denied;
}

bool ProxyFilter::checkCredentials(const std::string& username, const std::string& password) {
  // Single policy for both auth entry points (``AUTH`` and ``HELLO N AUTH``):
  //   - no configured username: the default user may be named "default" (Redis 6 ACL synonym)
  //     or left empty; only the password is checked.
  //   - configured username: exact match.
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

void ProxyFilter::PendingRequest::setDownstreamRespVersion(uint32_t version) {
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
