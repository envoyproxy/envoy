#include "extensions/filters/http/ext_authz/ext_authz.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/http/utility.h"
#include "common/router/config_impl.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

struct RcDetailsValues {
  // The ext_authz filter denied the downstream request.
  const std::string AuthzDenied = "ext_authz_denied";
  // The ext_authz filter encountered a failure, and was configured to fail-closed.
  const std::string AuthzError = "ext_authz_error";
};
using RcDetails = ConstSingleton<RcDetailsValues>;

void FilterConfigPerRoute::merge(const FilterConfigPerRoute& other) {
  disabled_ = other.disabled_;
  auto begin_it = other.context_extensions_.begin();
  auto end_it = other.context_extensions_.end();
  for (auto it = begin_it; it != end_it; ++it) {
    context_extensions_[it->first] = it->second;
  }
}

void Filter::initiateCall(const Http::HeaderMap& headers) {
  if (filter_return_ == FilterReturn::StopDecoding) {
    return;
  }

  Router::RouteConstSharedPtr route = callbacks_->route();
  if (route == nullptr || route->routeEntry() == nullptr) {
    return;
  }
  cluster_ = callbacks_->clusterInfo();

  // Fast route - if we are disabled, no need to merge.
  const FilterConfigPerRoute* specific_per_route_config =
      Http::Utility::resolveMostSpecificPerFilterConfig<FilterConfigPerRoute>(
          HttpFilterNames::get().ExtAuthorization, route);
  if (specific_per_route_config != nullptr) {
    if (specific_per_route_config->disabled()) {
      return;
    }
  }

  // We are not disabled - get a merged view of the config:
  auto&& maybe_merged_per_route_config =
      Http::Utility::getMergedPerFilterConfig<FilterConfigPerRoute>(
          HttpFilterNames::get().ExtAuthorization, route,
          [](FilterConfigPerRoute& cfg_base, const FilterConfigPerRoute& cfg) {
            cfg_base.merge(cfg);
          });

  Protobuf::Map<std::string, std::string> context_extensions;
  if (maybe_merged_per_route_config) {
    context_extensions = maybe_merged_per_route_config.value().takeContextExtensions();
  }

  // If metadata_context_namespaces is specified, pass matching metadata to the ext_authz service
  envoy::api::v2::core::Metadata metadata_context;
  const auto& request_metadata = callbacks_->streamInfo().dynamicMetadata().filter_metadata();
  for (const auto& context_key : config_->metadataContextNamespaces()) {
    const auto& metadata_it = request_metadata.find(context_key);
    if (metadata_it != request_metadata.end()) {
      (*metadata_context.mutable_filter_metadata())[metadata_it->first] = metadata_it->second;
    }
  }

  Filters::Common::ExtAuthz::CheckRequestUtils::createHttpCheck(
      callbacks_, headers, std::move(context_extensions), std::move(metadata_context),
      check_request_, config_->maxRequestBytes());

  ENVOY_STREAM_LOG(trace, "ext_authz filter calling authorization server", *callbacks_);
  state_ = State::Calling;
  filter_return_ = FilterReturn::StopDecoding; // Don't let the filter chain continue as we are
                                               // going to invoke check call.
  initiating_call_ = true;
  client_->check(*this, check_request_, callbacks_->activeSpan());
  initiating_call_ = false;
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::HeaderMap& headers, bool end_stream) {
  if (!config_->filterEnabled()) {
    return Http::FilterHeadersStatus::Continue;
  }

  request_headers_ = &headers;
  buffer_data_ = config_->withRequestBody() &&
                 !(end_stream || Http::Utility::isWebSocketUpgradeRequest(headers) ||
                   Http::Utility::isH2UpgradeRequest(headers));
  if (buffer_data_) {
    ENVOY_STREAM_LOG(debug, "ext_authz filter is buffering the request", *callbacks_);
    if (!config_->allowPartialMessage()) {
      callbacks_->setDecoderBufferLimit(config_->maxRequestBytes());
    }
    return Http::FilterHeadersStatus::StopIteration;
  }

  initiateCall(headers);
  return filter_return_ == FilterReturn::StopDecoding
             ? Http::FilterHeadersStatus::StopAllIterationAndWatermark
             : Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance&, bool end_stream) {
  if (buffer_data_) {
    const bool buffer_is_full = isBufferFull();
    if (end_stream || buffer_is_full) {
      ENVOY_STREAM_LOG(debug, "ext_authz filter finished buffering the request since {}",
                       *callbacks_, buffer_is_full ? "buffer is full" : "stream is ended");
      initiateCall(*request_headers_);
      return filter_return_ == FilterReturn::StopDecoding
                 ? Http::FilterDataStatus::StopIterationAndWatermark
                 : Http::FilterDataStatus::Continue;
    } else {
      return Http::FilterDataStatus::StopIterationAndBuffer;
    }
  }

  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus Filter::decodeTrailers(Http::HeaderMap&) {
  if (buffer_data_) {
    if (filter_return_ != FilterReturn::StopDecoding) {
      ENVOY_STREAM_LOG(debug, "ext_authz filter finished buffering the request", *callbacks_);
      initiateCall(*request_headers_);
    }
    return filter_return_ == FilterReturn::StopDecoding ? Http::FilterTrailersStatus::StopIteration
                                                        : Http::FilterTrailersStatus::Continue;
  }

  return Http::FilterTrailersStatus::Continue;
}

void Filter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

void Filter::onDestroy() {
  if (state_ == State::Calling) {
    state_ = State::Complete;
    client_->cancel();
  }
}

void Filter::onComplete(Filters::Common::ExtAuthz::ResponsePtr&& response) {
  state_ = State::Complete;
  using Filters::Common::ExtAuthz::CheckStatus;
  Stats::StatName empty_stat_name;

  switch (response->status) {
  case CheckStatus::OK: {
    ENVOY_STREAM_LOG(trace, "ext_authz filter added header(s) to the request:", *callbacks_);
    if (config_->clearRouteCache() &&
        (!response->headers_to_add.empty() || !response->headers_to_append.empty())) {
      ENVOY_STREAM_LOG(debug, "ext_authz is clearing route cache", *callbacks_);
      callbacks_->clearRouteCache();
    }
    for (const auto& header : response->headers_to_add) {
      ENVOY_STREAM_LOG(trace, " '{}':'{}'", *callbacks_, header.first.get(), header.second);
      Http::HeaderEntry* header_to_modify = request_headers_->get(header.first);
      if (header_to_modify) {
        header_to_modify->value(header.second.c_str(), header.second.size());
      } else {
        request_headers_->addCopy(header.first, header.second);
      }
    }
    for (const auto& header : response->headers_to_append) {
      Http::HeaderEntry* header_to_modify = request_headers_->get(header.first);
      if (header_to_modify) {
        ENVOY_STREAM_LOG(trace, " '{}':'{}'", *callbacks_, header.first.get(), header.second);
        Http::HeaderMapImpl::appendToHeader(header_to_modify->value(), header.second);
      }
    }
    if (cluster_) {
      config_->incCounter(cluster_->statsScope(), config_->ext_authz_ok_);
    }
    stats_.ok_.inc();
    continueDecoding();
    break;
  }

  case CheckStatus::Denied: {
    ENVOY_STREAM_LOG(trace, "ext_authz filter rejected the request. Response status code: '{}",
                     *callbacks_, enumToInt(response->status_code));
    stats_.denied_.inc();

    if (cluster_) {
      config_->incCounter(cluster_->statsScope(), config_->ext_authz_denied_);

      Http::CodeStats::ResponseStatInfo info{config_->scope(),
                                             cluster_->statsScope(),
                                             empty_stat_name,
                                             enumToInt(response->status_code),
                                             true,
                                             empty_stat_name,
                                             empty_stat_name,
                                             empty_stat_name,
                                             empty_stat_name,
                                             false};
      config_->httpContext().codeStats().chargeResponseStat(info);
    }

    callbacks_->sendLocalReply(
        response->status_code, response->body,
        [& headers = response->headers_to_add,
         &callbacks = *callbacks_](Http::HeaderMap& response_headers) -> void {
          ENVOY_STREAM_LOG(trace,
                           "ext_authz filter added header(s) to the local response:", callbacks);
          for (const auto& header : headers) {
            ENVOY_STREAM_LOG(trace, " '{}':'{}'", callbacks, header.first.get(), header.second);
            response_headers.remove(header.first);
            response_headers.addCopy(header.first, header.second);
          }
        },
        absl::nullopt, RcDetails::get().AuthzDenied);
    callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::UnauthorizedExternalService);
    break;
  }

  case CheckStatus::Error: {
    if (cluster_) {
      config_->incCounter(cluster_->statsScope(), config_->ext_authz_error_);
    }
    stats_.error_.inc();
    if (config_->failureModeAllow()) {
      ENVOY_STREAM_LOG(trace, "ext_authz filter allowed the request with error", *callbacks_);
      stats_.failure_mode_allowed_.inc();
      if (cluster_) {
        config_->incCounter(cluster_->statsScope(), config_->ext_authz_failure_mode_allowed_);
      }
      continueDecoding();
    } else {
      ENVOY_STREAM_LOG(
          trace, "ext_authz filter rejected the request with an error. Response status code: {}",
          *callbacks_, enumToInt(config_->statusOnError()));
      callbacks_->streamInfo().setResponseFlag(
          StreamInfo::ResponseFlag::UnauthorizedExternalService);
      callbacks_->sendLocalReply(config_->statusOnError(), EMPTY_STRING, nullptr, absl::nullopt,
                                 RcDetails::get().AuthzError);
    }
    break;
  }

  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
    break;
  }
}

bool Filter::isBufferFull() {
  const auto* buffer = callbacks_->decodingBuffer();
  if (config_->allowPartialMessage() && buffer != nullptr) {
    return buffer->length() >= config_->maxRequestBytes();
  }
  return false;
}

void Filter::continueDecoding() {
  filter_return_ = FilterReturn::ContinueDecoding;
  if (!initiating_call_) {
    callbacks_->continueDecoding();
  }
}

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
