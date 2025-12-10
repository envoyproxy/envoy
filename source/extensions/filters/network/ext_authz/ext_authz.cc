#include "source/extensions/filters/network/ext_authz/ext_authz.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/common/assert.h"
#include "source/common/tls/connection_info_impl_base.h"
#include "source/common/tracing/http_tracer_impl.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtAuthz {

namespace {

using MetadataProto = ::envoy::config::core::v3::Metadata;

void fillMetadataContext(const MetadataProto& source_metadata,
                         const std::vector<std::string>& metadata_context_namespaces,
                         const std::vector<std::string>& typed_metadata_context_namespaces,
                         MetadataProto& metadata_context) {
  for (const auto& context_key : metadata_context_namespaces) {
    const auto& filter_metadata = source_metadata.filter_metadata();
    if (const auto metadata_it = filter_metadata.find(context_key);
        metadata_it != filter_metadata.end()) {
      (*metadata_context.mutable_filter_metadata())[metadata_it->first] = metadata_it->second;
    }
  }

  for (const auto& context_key : typed_metadata_context_namespaces) {
    const auto& typed_filter_metadata = source_metadata.typed_filter_metadata();
    if (const auto metadata_it = typed_filter_metadata.find(context_key);
        metadata_it != typed_filter_metadata.end()) {
      (*metadata_context.mutable_typed_filter_metadata())[metadata_it->first] = metadata_it->second;
    }
  }
}

} // namespace

InstanceStats Config::generateStats(const std::string& name, Stats::Scope& scope) {
  const std::string final_prefix = fmt::format("ext_authz.{}.", name);
  return {ALL_TCP_EXT_AUTHZ_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                                  POOL_GAUGE_PREFIX(scope, final_prefix))};
}

void Filter::callCheck() {
  // If metadata_context_namespaces or typed_metadata_context_namespaces is specified,
  // pass matching filter metadata to the ext_authz service.
  envoy::config::core::v3::Metadata metadata_context;
  fillMetadataContext(filter_callbacks_->connection().streamInfo().dynamicMetadata(),
                      config_->metadataContextNamespaces(),
                      config_->typedMetadataContextNamespaces(), metadata_context);

  Filters::Common::ExtAuthz::CheckRequestUtils::createTcpCheck(
      filter_callbacks_, check_request_, config_->includePeerCertificate(),
      config_->includeTLSSession(), config_->destinationLabels(), std::move(metadata_context));
  // Store start time of ext_authz filter call
  start_time_ = filter_callbacks_->connection().dispatcher().timeSource().monotonicTime();
  status_ = Status::Calling;
  config_->stats().active_.inc();
  config_->stats().total_.inc();

  calling_check_ = true;
  client_->check(*this, check_request_, Tracing::NullSpan::instance(),
                 filter_callbacks_->connection().streamInfo());
  calling_check_ = false;
}

Network::FilterStatus Filter::onData(Buffer::Instance&, bool /* end_stream */) {
  if (!filterEnabled(filter_callbacks_->connection().streamInfo().dynamicMetadata())) {
    config_->stats().disabled_.inc();
    return Network::FilterStatus::Continue;
  }

  if (status_ == Status::NotStarted) {
    // By waiting to invoke the check at onData() the call to authorization service will have
    // sufficient information to fill out the checkRequest_.
    callCheck();
  }
  return filter_return_ == FilterReturn::Stop ? Network::FilterStatus::StopIteration
                                              : Network::FilterStatus::Continue;
}

Network::FilterStatus Filter::onNewConnection() {
  // Wait till onData() happens.
  return Network::FilterStatus::Continue;
}

void Filter::onEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    if (status_ == Status::Calling) {
      // Make sure that any pending request in the client is cancelled. This will be NOP if the
      // request already completed.
      client_->cancel();
      config_->stats().active_.dec();
    }
  }
}

void Filter::onComplete(Filters::Common::ExtAuthz::ResponsePtr&& response) {
  status_ = Status::Complete;
  config_->stats().active_.dec();

  switch (response->status) {
  case Filters::Common::ExtAuthz::CheckStatus::OK:
    config_->stats().ok_.inc();
    // Add duration of call to dynamic metadata if applicable
    if (start_time_.has_value()) {
      Protobuf::Value ext_authz_duration_value;
      auto duration = filter_callbacks_->connection().dispatcher().timeSource().monotonicTime() -
                      start_time_.value();
      ext_authz_duration_value.set_number_value(
          std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
      (*response->dynamic_metadata.mutable_fields())["ext_authz_duration"] =
          ext_authz_duration_value;
    }
    break;
  case Filters::Common::ExtAuthz::CheckStatus::Error:
    config_->stats().error_.inc();
    break;
  case Filters::Common::ExtAuthz::CheckStatus::Denied:
    config_->stats().denied_.inc();
    break;
  }

  if (!response->dynamic_metadata.fields().empty()) {

    filter_callbacks_->connection().streamInfo().setDynamicMetadata(
        NetworkFilterNames::get().ExtAuthorization, response->dynamic_metadata);
  }

  // Fail open only if configured to do so and if the check status was a error.
  if (response->status == Filters::Common::ExtAuthz::CheckStatus::Denied ||
      (response->status == Filters::Common::ExtAuthz::CheckStatus::Error &&
       !config_->failureModeAllow())) {
    config_->stats().cx_closed_.inc();

    if (config_->sendTlsAlertOnDenial()) {
      auto ssl_info = filter_callbacks_->connection().ssl();
      if (ssl_info != nullptr) {
        auto* ssl_conn_info =
            dynamic_cast<const Extensions::TransportSockets::Tls::ConnectionInfoImplBase*>(
                ssl_info.get());
        if (ssl_conn_info != nullptr) {
          SSL* ssl = ssl_conn_info->ssl();
          if (ssl != nullptr) {
            SSL_send_fatal_alert(ssl, SSL_AD_ACCESS_DENIED);
          }
        }
      }
    }

    filter_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush, "ext_authz_close");
    filter_callbacks_->connection().streamInfo().setResponseFlag(
        StreamInfo::CoreResponseFlag::UnauthorizedExternalService);
    filter_callbacks_->connection().streamInfo().setResponseCodeDetails(
        response->status == Filters::Common::ExtAuthz::CheckStatus::Denied
            ? Filters::Common::ExtAuthz::ResponseCodeDetails::get().AuthzDenied
            : Filters::Common::ExtAuthz::ResponseCodeDetails::get().AuthzError);
  } else {
    // Let the filter chain continue.
    filter_return_ = FilterReturn::Continue;
    if (config_->failureModeAllow() &&
        response->status == Filters::Common::ExtAuthz::CheckStatus::Error) {
      // Status is Error and yet we are configured to allow traffic. Click a counter.
      config_->stats().failure_mode_allowed_.inc();
    }

    // We can get completion inline, so only call continue if that isn't happening.
    if (!calling_check_) {
      filter_callbacks_->continueReading();
    }
  }
}

} // namespace ExtAuthz
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
