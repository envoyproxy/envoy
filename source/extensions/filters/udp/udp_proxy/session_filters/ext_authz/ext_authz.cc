#include "source/extensions/filters/udp/udp_proxy/session_filters/ext_authz/ext_authz.h"

#include <chrono>

#include "envoy/common/exception.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/network/listener.h"

#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/tracing/null_span_impl.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace ExtAuthz {

constexpr uint32_t DefaultMaxBufferedDatagrams = 1024;
constexpr uint64_t DefaultMaxBufferedBytes = 16384;

Config::Config(const FilterConfig& config, Stats::Scope& scope,
               Server::Configuration::ServerFactoryContext& context)
    : stats_scope_(
          scope.createScope(absl::StrCat("udp.session.ext_authz.", config.stat_prefix(), "."))),
      stats_(generateStats(*stats_scope_)), failure_mode_allow_(config.failure_mode_allow()),
      timeout_(std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(config.grpc_service(), timeout, 200))),
      buffer_enabled_(config.has_buffer_options()),
      max_buffered_datagrams_(config.has_buffer_options()
                                  ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.buffer_options(),
                                                                    max_buffered_datagrams,
                                                                    DefaultMaxBufferedDatagrams)
                                  : DefaultMaxBufferedDatagrams),
      max_buffered_bytes_(config.has_buffer_options()
                              ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.buffer_options(),
                                                                max_buffered_bytes,
                                                                DefaultMaxBufferedBytes)
                              : DefaultMaxBufferedBytes),
      async_client_factory_(createAsyncClientFactory(config, scope, context)) {}

Grpc::AsyncClientFactoryPtr
Config::createAsyncClientFactory(const FilterConfig& config, Stats::Scope& scope,
                                 Server::Configuration::ServerFactoryContext& context) {
  auto factory_or_error = context.clusterManager().grpcAsyncClientManager().factoryForGrpcService(
      config.grpc_service(), scope, true);
  THROW_IF_NOT_OK_REF(factory_or_error.status());
  return std::move(factory_or_error.value());
}

Filters::Common::ExtAuthz::ClientPtr Config::createClient() const {
  return std::make_unique<Filters::Common::ExtAuthz::GrpcClientImpl>(
      THROW_OR_RETURN_VALUE(async_client_factory_->createUncachedRawAsyncClient(),
                            Grpc::RawAsyncClientPtr),
      timeout_);
}

Filter::~Filter() {
  if (status_ == Status::Calling) {
    // onComplete() won't run for a cancelled check, so release the active gauge here.
    client_->cancel();
    config_->stats().active_.dec();
  }
}

ReadFilterStatus Filter::onNewSession() {
  auto& attrs = *check_request_.mutable_attributes();
  const auto& provider = read_callbacks_->streamInfo().downstreamAddressProvider();
  Network::Utility::addressToProtobufAddress(*provider.remoteAddress(),
                                             *attrs.mutable_source()->mutable_address());
  Network::Utility::addressToProtobufAddress(*provider.localAddress(),
                                             *attrs.mutable_destination()->mutable_address());

  status_ = Status::Calling;
  config_->stats().total_.inc();
  config_->stats().active_.inc();

  // check() may invoke onComplete() inline, so calling_check_ signals that case to it.
  calling_check_ = true;
  client_->check(*this, check_request_, Tracing::NullSpan::instance(),
                 read_callbacks_->streamInfo());
  calling_check_ = false;

  if (completed_ && allowed_) {
    return ReadFilterStatus::Continue;
  }
  return ReadFilterStatus::StopIteration;
}

ReadFilterStatus Filter::onData(Network::UdpRecvData& data) {
  if (completed_ && allowed_) {
    return ReadFilterStatus::Continue;
  }

  // Buffered while the check is pending, then replayed by onComplete() on allow.
  if (!completed_) {
    maybeBufferDatagram(data);
  }
  return ReadFilterStatus::StopIteration;
}

void Filter::onComplete(Filters::Common::ExtAuthz::ResponsePtr&& response) {
  status_ = Status::Complete;
  completed_ = true;
  config_->stats().active_.dec();

  switch (response->status) {
  case Filters::Common::ExtAuthz::CheckStatus::OK:
    config_->stats().ok_.inc();
    allowed_ = true;
    break;
  case Filters::Common::ExtAuthz::CheckStatus::Error:
    config_->stats().error_.inc();
    allowed_ = config_->failureModeAllow();
    if (allowed_) {
      config_->stats().failure_mode_allowed_.inc();
    }
    break;
  case Filters::Common::ExtAuthz::CheckStatus::Denied:
    config_->stats().denied_.inc();
    allowed_ = false;
    break;
  }

  if (!response->dynamic_metadata.fields().empty()) {
    read_callbacks_->streamInfo().setDynamicMetadata(std::string(FilterName),
                                                     response->dynamic_metadata);
  }

  if (!allowed_) {
    read_callbacks_->streamInfo().setResponseFlag(
        StreamInfo::CoreResponseFlag::UnauthorizedExternalService);
    clearBuffer();
    return;
  }

  // A synchronous allow is driven by onNewSession()'s return value, not here.
  if (calling_check_) {
    return;
  }

  // On false the session is gone and this filter may be destroyed, so touch nothing after.
  if (!read_callbacks_->continueFilterChain()) {
    return;
  }

  while (!datagrams_buffer_.empty()) {
    BufferedDatagramPtr datagram = std::move(datagrams_buffer_.front());
    datagrams_buffer_.pop();
    read_callbacks_->injectDatagramToFilterChain(*datagram);
  }
  disableSessionBuffer();
  buffered_bytes_ = 0;
}

void Filter::maybeBufferDatagram(Network::UdpRecvData& data) {
  if (!sessionBufferEnabled()) {
    return;
  }

  if (datagrams_buffer_.size() == config_->maxBufferedDatagrams() ||
      buffered_bytes_ + data.buffer_->length() > config_->maxBufferedBytes()) {
    config_->stats().buffer_overflow_.inc();
    return;
  }

  auto buffered_datagram = std::make_unique<Network::UdpRecvData>();
  buffered_datagram->addresses_ = {std::move(data.addresses_.local_),
                                   std::move(data.addresses_.peer_)};
  buffered_datagram->buffer_ = std::move(data.buffer_);
  buffered_datagram->receive_time_ = data.receive_time_;
  buffered_bytes_ += buffered_datagram->buffer_->length();
  datagrams_buffer_.push(std::move(buffered_datagram));
}

void Filter::clearBuffer() {
  std::queue<BufferedDatagramPtr> empty;
  datagrams_buffer_.swap(empty);
  buffered_bytes_ = 0;
}

} // namespace ExtAuthz
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
