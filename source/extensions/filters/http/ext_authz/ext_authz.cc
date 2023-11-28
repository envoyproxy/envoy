#include "source/extensions/filters/http/ext_authz/ext_authz.h"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/matchers.h"
#include "source/common/http/utility.h"
#include "source/common/router/config_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

namespace {

using MetadataProto = ::envoy::config::core::v3::Metadata;

void fillMetadataContext(const std::vector<const MetadataProto*>& source_metadata,
                         const std::vector<std::string>& metadata_context_namespaces,
                         const std::vector<std::string>& typed_metadata_context_namespaces,
                         MetadataProto& metadata_context) {
  for (const auto& context_key : metadata_context_namespaces) {
    for (const MetadataProto* metadata : source_metadata) {
      if (metadata == nullptr) {
        continue;
      }
      const auto& filter_metadata = metadata->filter_metadata();
      if (const auto metadata_it = filter_metadata.find(context_key);
          metadata_it != filter_metadata.end()) {
        (*metadata_context.mutable_filter_metadata())[metadata_it->first] = metadata_it->second;
        break;
      }
    }
  }

  for (const auto& context_key : typed_metadata_context_namespaces) {
    for (const MetadataProto* metadata : source_metadata) {
      if (metadata == nullptr) {
        continue;
      }
      const auto& typed_filter_metadata = metadata->typed_filter_metadata();
      if (const auto metadata_it = typed_filter_metadata.find(context_key);
          metadata_it != typed_filter_metadata.end()) {
        (*metadata_context.mutable_typed_filter_metadata())[metadata_it->first] =
            metadata_it->second;
        break;
      }
    }
  }
}

} // namespace

void FilterConfigPerRoute::merge(const FilterConfigPerRoute& other) {
  // We only merge context extensions here, and leave boolean flags untouched since those flags are
  // not used from the merged config.
  auto begin_it = other.context_extensions_.begin();
  auto end_it = other.context_extensions_.end();
  for (auto it = begin_it; it != end_it; ++it) {
    context_extensions_[it->first] = it->second;
  }
}

void Filter::initiateCall(const Http::RequestHeaderMap& headers) {
  if (filter_return_ == FilterReturn::StopDecoding) {
    return;
  }

  auto&& maybe_merged_per_route_config =
      Http::Utility::getMergedPerFilterConfig<FilterConfigPerRoute>(
          decoder_callbacks_, [](FilterConfigPerRoute& cfg_base, const FilterConfigPerRoute& cfg) {
            cfg_base.merge(cfg);
          });

  Protobuf::Map<std::string, std::string> context_extensions;
  if (maybe_merged_per_route_config) {
    context_extensions = maybe_merged_per_route_config.value().takeContextExtensions();
  }

  // If metadata_context_namespaces or typed_metadata_context_namespaces is specified,
  // pass matching filter metadata to the ext_authz service.
  // If metadata key is set in both the connection and request metadata,
  // then the value will be the request metadata value.
  envoy::config::core::v3::Metadata metadata_context;
  fillMetadataContext({&decoder_callbacks_->streamInfo().dynamicMetadata(),
                       &decoder_callbacks_->connection()->streamInfo().dynamicMetadata()},
                      config_->metadataContextNamespaces(),
                      config_->typedMetadataContextNamespaces(), metadata_context);

  // Fill route_metadata_context from the selected route's metadata.
  envoy::config::core::v3::Metadata route_metadata_context;
  if (decoder_callbacks_->route() != nullptr) {
    fillMetadataContext({&decoder_callbacks_->route()->metadata()},
                        config_->routeMetadataContextNamespaces(),
                        config_->routeTypedMetadataContextNamespaces(), route_metadata_context);
  }

  Filters::Common::ExtAuthz::CheckRequestUtils::createHttpCheck(
      decoder_callbacks_, headers, std::move(context_extensions), std::move(metadata_context),
      std::move(route_metadata_context), check_request_, config_->maxRequestBytes(),
      config_->packAsBytes(), config_->includePeerCertificate(), config_->includeTLSSession(),
      config_->destinationLabels(), config_->requestHeaderMatchers());

  ENVOY_STREAM_LOG(trace, "ext_authz filter calling authorization server", *decoder_callbacks_);
  // Store start time of ext_authz filter call
  start_time_ = decoder_callbacks_->dispatcher().timeSource().monotonicTime();

  state_ = State::Calling;
  filter_return_ = FilterReturn::StopDecoding; // Don't let the filter chain continue as we are
                                               // going to invoke check call.
  cluster_ = decoder_callbacks_->clusterInfo();
  initiating_call_ = true;
  client_->check(*this, check_request_, decoder_callbacks_->activeSpan(),
                 decoder_callbacks_->streamInfo());
  initiating_call_ = false;
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool end_stream) {
  Router::RouteConstSharedPtr route = decoder_callbacks_->route();
  const auto per_route_flags = getPerRouteFlags(route);
  skip_check_ = per_route_flags.skip_check_;
  if (skip_check_) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (!config_->filterEnabled(decoder_callbacks_->streamInfo().dynamicMetadata())) {
    stats_.disabled_.inc();
    if (config_->denyAtDisable()) {
      ENVOY_STREAM_LOG(trace, "ext_authz filter is disabled. Deny the request.",
                       *decoder_callbacks_);
      decoder_callbacks_->streamInfo().setResponseFlag(
          StreamInfo::ResponseFlag::UnauthorizedExternalService);
      decoder_callbacks_->sendLocalReply(
          config_->statusOnError(), EMPTY_STRING, nullptr, absl::nullopt,
          Filters::Common::ExtAuthz::ResponseCodeDetails::get().AuthzError);
      return Http::FilterHeadersStatus::StopIteration;
    }
    return Http::FilterHeadersStatus::Continue;
  }

  request_headers_ = &headers;
  const auto check_settings = per_route_flags.check_settings_;
  buffer_data_ = (config_->withRequestBody() || check_settings.has_with_request_body()) &&
                 !check_settings.disable_request_body_buffering() &&
                 !(end_stream || Http::Utility::isWebSocketUpgradeRequest(headers) ||
                   Http::Utility::isH2UpgradeRequest(headers));

  if (buffer_data_) {
    ENVOY_STREAM_LOG(debug, "ext_authz filter is buffering the request", *decoder_callbacks_);

    const auto allow_partial_message =
        check_settings.has_with_request_body()
            ? check_settings.with_request_body().allow_partial_message()
            : config_->allowPartialMessage();
    const auto max_request_bytes = check_settings.has_with_request_body()
                                       ? check_settings.with_request_body().max_request_bytes()
                                       : config_->maxRequestBytes();

    if (!allow_partial_message) {
      decoder_callbacks_->setDecoderBufferLimit(max_request_bytes);
    }
    return Http::FilterHeadersStatus::StopIteration;
  }

  // Initiate a call to the authorization server since we are not disabled.
  initiateCall(headers);
  return filter_return_ == FilterReturn::StopDecoding
             ? Http::FilterHeadersStatus::StopAllIterationAndWatermark
             : Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance& data, bool end_stream) {
  if (buffer_data_ && !skip_check_) {
    const bool buffer_is_full = isBufferFull(data.length());
    if (end_stream || buffer_is_full) {
      ENVOY_STREAM_LOG(debug, "ext_authz filter finished buffering the request since {}",
                       *decoder_callbacks_, buffer_is_full ? "buffer is full" : "stream is ended");
      // Make sure data is available in initiateCall.
      decoder_callbacks_->addDecodedData(data, true);
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

Http::FilterTrailersStatus Filter::decodeTrailers(Http::RequestTrailerMap&) {
  if (buffer_data_ && !skip_check_) {
    if (filter_return_ != FilterReturn::StopDecoding) {
      ENVOY_STREAM_LOG(debug, "ext_authz filter finished buffering the request",
                       *decoder_callbacks_);
      initiateCall(*request_headers_);
    }
    return filter_return_ == FilterReturn::StopDecoding ? Http::FilterTrailersStatus::StopIteration
                                                        : Http::FilterTrailersStatus::Continue;
  }

  return Http::FilterTrailersStatus::Continue;
}

Http::Filter1xxHeadersStatus Filter::encode1xxHeaders(Http::ResponseHeaderMap&) {
  return Http::Filter1xxHeadersStatus::Continue;
}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  ENVOY_STREAM_LOG(trace,
                   "ext_authz filter has {} response header(s) to add and {} response header(s) to "
                   "set to the encoded response:",
                   *encoder_callbacks_, response_headers_to_add_.size(),
                   response_headers_to_set_.size());
  if (!response_headers_to_add_.empty()) {
    ENVOY_STREAM_LOG(
        trace, "ext_authz filter added header(s) to the encoded response:", *encoder_callbacks_);
    for (const auto& header : response_headers_to_add_) {
      ENVOY_STREAM_LOG(trace, "'{}':'{}'", *encoder_callbacks_, header.first.get(), header.second);
      headers.addCopy(header.first, header.second);
    }
  }

  if (!response_headers_to_set_.empty()) {
    ENVOY_STREAM_LOG(
        trace, "ext_authz filter set header(s) to the encoded response:", *encoder_callbacks_);
    for (const auto& header : response_headers_to_set_) {
      ENVOY_STREAM_LOG(trace, "'{}':'{}'", *encoder_callbacks_, header.first.get(), header.second);
      headers.setCopy(header.first, header.second);
    }
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::encodeData(Buffer::Instance&, bool) {
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus Filter::encodeTrailers(Http::ResponseTrailerMap&) {
  return Http::FilterTrailersStatus::Continue;
}

Http::FilterMetadataStatus Filter::encodeMetadata(Http::MetadataMap&) {
  return Http::FilterMetadataStatus::Continue;
}

void Filter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

void Filter::setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) {
  encoder_callbacks_ = &callbacks;
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

  if (!response->dynamic_metadata.fields().empty()) {
    // Add duration of call to dynamic metadata if applicable
    if (start_time_.has_value() && response->status == CheckStatus::OK) {
      ProtobufWkt::Value ext_authz_duration_value;
      auto duration =
          decoder_callbacks_->dispatcher().timeSource().monotonicTime() - start_time_.value();
      ext_authz_duration_value.set_number_value(
          std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
      (*response->dynamic_metadata.mutable_fields())["ext_authz_duration"] =
          ext_authz_duration_value;
    }
    decoder_callbacks_->streamInfo().setDynamicMetadata("envoy.filters.http.ext_authz",
                                                        response->dynamic_metadata);
  }

  switch (response->status) {
  case CheckStatus::OK: {
    // Any changes to request headers or query parameters can affect how the request is going to be
    // routed. If we are changing the headers we also need to clear the route
    // cache.
    if (config_->clearRouteCache() &&
        (!response->headers_to_set.empty() || !response->headers_to_append.empty() ||
         !response->headers_to_remove.empty() || !response->query_parameters_to_set.empty() ||
         !response->query_parameters_to_remove.empty())) {
      ENVOY_STREAM_LOG(debug, "ext_authz is clearing route cache", *decoder_callbacks_);
      decoder_callbacks_->downstreamCallbacks()->clearRouteCache();
    }

    ENVOY_STREAM_LOG(trace,
                     "ext_authz filter added header(s) to the request:", *decoder_callbacks_);
    for (const auto& header : response->headers_to_set) {
      ENVOY_STREAM_LOG(trace, "'{}':'{}'", *decoder_callbacks_, header.first.get(), header.second);
      request_headers_->setCopy(header.first, header.second);
    }
    for (const auto& header : response->headers_to_add) {
      ENVOY_STREAM_LOG(trace, "'{}':'{}'", *decoder_callbacks_, header.first.get(), header.second);
      request_headers_->addCopy(header.first, header.second);
    }
    for (const auto& header : response->headers_to_append) {
      const auto header_to_modify = request_headers_->get(header.first);
      // TODO(dio): Add a flag to allow appending non-existent headers, without setting it first
      // (via `headers_to_add`). For example, given:
      // 1. Original headers {"original": "true"}
      // 2. Response headers from the authorization servers {{"append": "1"}, {"append": "2"}}
      //
      // Currently it is not possible to add {{"append": "1"}, {"append": "2"}} (the intended
      // combined headers: {{"original": "true"}, {"append": "1"}, {"append": "2"}}) to the request
      // to upstream server by only sets `headers_to_append`.
      if (!header_to_modify.empty()) {
        ENVOY_STREAM_LOG(trace, "'{}':'{}'", *decoder_callbacks_, header.first.get(),
                         header.second);
        // The current behavior of appending is by combining entries with the same key, into one
        // entry. The value of that combined entry is separated by ",".
        // TODO(dio): Consider to use addCopy instead.
        request_headers_->appendCopy(header.first, header.second);
      }
    }

    ENVOY_STREAM_LOG(trace,
                     "ext_authz filter removed header(s) from the request:", *decoder_callbacks_);
    for (const auto& header : response->headers_to_remove) {
      // We don't allow removing any :-prefixed headers, nor Host, as removing
      // them would make the request malformed.
      if (!Http::HeaderUtility::isRemovableHeader(header.get())) {
        continue;
      }
      ENVOY_STREAM_LOG(trace, "'{}'", *decoder_callbacks_, header.get());
      request_headers_->remove(header);
    }

    if (!response->response_headers_to_add.empty()) {
      ENVOY_STREAM_LOG(trace, "ext_authz filter saving {} header(s) to add to the response:",
                       *decoder_callbacks_, response->response_headers_to_add.size());
      response_headers_to_add_ = std::move(response->response_headers_to_add);
    }

    if (!response->response_headers_to_set.empty()) {
      ENVOY_STREAM_LOG(trace, "ext_authz filter saving {} header(s) to set to the response:",
                       *decoder_callbacks_, response->response_headers_to_set.size());
      response_headers_to_set_ = std::move(response->response_headers_to_set);
    }

    absl::optional<Http::Utility::QueryParamsMulti> modified_query_parameters;
    if (!response->query_parameters_to_set.empty()) {
      modified_query_parameters = Http::Utility::QueryParamsMulti::parseQueryString(
          request_headers_->Path()->value().getStringView());
      ENVOY_STREAM_LOG(
          trace, "ext_authz filter set query parameter(s) on the request:", *decoder_callbacks_);
      for (const auto& [key, value] : response->query_parameters_to_set) {
        ENVOY_STREAM_LOG(trace, "'{}={}'", *decoder_callbacks_, key, value);
        modified_query_parameters->overwrite(key, value);
      }
    }

    if (!response->query_parameters_to_remove.empty()) {
      if (!modified_query_parameters) {
        modified_query_parameters = Http::Utility::QueryParamsMulti::parseQueryString(
            request_headers_->Path()->value().getStringView());
      }
      ENVOY_STREAM_LOG(trace, "ext_authz filter removed query parameter(s) from the request:",
                       *decoder_callbacks_);
      for (const auto& key : response->query_parameters_to_remove) {
        ENVOY_STREAM_LOG(trace, "'{}'", *decoder_callbacks_, key);
        modified_query_parameters->remove(key);
      }
    }

    // We modified the query parameters in some way, so regenerate the `path` header and set it
    // here.
    if (modified_query_parameters) {
      const auto new_path =
          modified_query_parameters->replaceQueryString(request_headers_->Path()->value());
      ENVOY_STREAM_LOG(
          trace, "ext_authz filter modified query parameter(s), using new path for request: {}",
          *decoder_callbacks_, new_path);
      request_headers_->setPath(new_path);
    }

    if (cluster_) {
      config_->incCounter(cluster_->statsScope(), config_->ext_authz_ok_);
    }
    stats_.ok_.inc();
    continueDecoding();
    break;
  }

  case CheckStatus::Denied: {
    ENVOY_STREAM_LOG(trace, "ext_authz filter rejected the request. Response status code: '{}'",
                     *decoder_callbacks_, enumToInt(response->status_code));
    stats_.denied_.inc();

    if (cluster_) {
      config_->incCounter(cluster_->statsScope(), config_->ext_authz_denied_);
      if (config_->chargeClusterResponseStats()) {
        Http::CodeStats::ResponseStatInfo info{config_->scope(),
                                               cluster_->statsScope(),
                                               empty_stat_name,
                                               enumToInt(response->status_code),
                                               true,
                                               empty_stat_name,
                                               empty_stat_name,
                                               empty_stat_name,
                                               empty_stat_name,
                                               empty_stat_name,
                                               false};
        config_->httpContext().codeStats().chargeResponseStat(info, false);
      }
    }

    // setResponseFlag must be called before sendLocalReply
    decoder_callbacks_->streamInfo().setResponseFlag(
        StreamInfo::ResponseFlag::UnauthorizedExternalService);
    decoder_callbacks_->sendLocalReply(
        response->status_code, response->body,
        [&headers = response->headers_to_set,
         &callbacks = *decoder_callbacks_](Http::HeaderMap& response_headers) -> void {
          ENVOY_STREAM_LOG(trace,
                           "ext_authz filter added header(s) to the local response:", callbacks);
          // Firstly, remove all headers requested by the ext_authz filter, to ensure that they will
          // override existing headers.
          for (const auto& header : headers) {
            response_headers.remove(header.first);
          }
          // Then set all of the requested headers, allowing the same header to be set multiple
          // times, e.g. `Set-Cookie`.
          for (const auto& header : headers) {
            ENVOY_STREAM_LOG(trace, " '{}':'{}'", callbacks, header.first.get(), header.second);
            response_headers.addCopy(header.first, header.second);
          }
        },
        absl::nullopt, Filters::Common::ExtAuthz::ResponseCodeDetails::get().AuthzDenied);
    break;
  }

  case CheckStatus::Error: {
    if (cluster_) {
      config_->incCounter(cluster_->statsScope(), config_->ext_authz_error_);
    }
    stats_.error_.inc();
    if (config_->failureModeAllow()) {
      ENVOY_STREAM_LOG(trace, "ext_authz filter allowed the request with error",
                       *decoder_callbacks_);
      stats_.failure_mode_allowed_.inc();
      if (cluster_) {
        config_->incCounter(cluster_->statsScope(), config_->ext_authz_failure_mode_allowed_);
      }
      if (config_->failureModeAllowHeaderAdd()) {
        request_headers_->addReferenceKey(
            Filters::Common::ExtAuthz::Headers::get().EnvoyAuthFailureModeAllowed, "true");
      }
      continueDecoding();
    } else {
      ENVOY_STREAM_LOG(
          trace, "ext_authz filter rejected the request with an error. Response status code: {}",
          *decoder_callbacks_, enumToInt(config_->statusOnError()));
      decoder_callbacks_->streamInfo().setResponseFlag(
          StreamInfo::ResponseFlag::UnauthorizedExternalService);
      decoder_callbacks_->sendLocalReply(
          config_->statusOnError(), EMPTY_STRING, nullptr, absl::nullopt,
          Filters::Common::ExtAuthz::ResponseCodeDetails::get().AuthzError);
    }
    break;
  }
  }
}

bool Filter::isBufferFull(uint64_t num_bytes_processing) const {
  if (!config_->allowPartialMessage()) {
    return false;
  }

  uint64_t num_bytes_buffered = num_bytes_processing;
  const auto* buffer = decoder_callbacks_->decodingBuffer();
  if (buffer != nullptr) {
    num_bytes_buffered += buffer->length();
  }

  return num_bytes_buffered >= config_->maxRequestBytes();
}

void Filter::continueDecoding() {
  // After sending the check request, we don't need to buffer the data anymore.
  buffer_data_ = false;

  filter_return_ = FilterReturn::ContinueDecoding;
  if (!initiating_call_) {
    decoder_callbacks_->continueDecoding();
  }
}

Filter::PerRouteFlags Filter::getPerRouteFlags(const Router::RouteConstSharedPtr& route) const {
  if (route == nullptr) {
    return PerRouteFlags{
        true /*skip_check_*/,
        envoy::extensions::filters::http::ext_authz::v3::CheckSettings() /*check_settings_*/};
  }

  const auto* specific_check_settings =
      Http::Utility::resolveMostSpecificPerFilterConfig<FilterConfigPerRoute>(decoder_callbacks_);
  if (specific_check_settings != nullptr) {
    return PerRouteFlags{specific_check_settings->disabled(),
                         specific_check_settings->checkSettings()};
  }

  return PerRouteFlags{
      false /*skip_check_*/,
      envoy::extensions::filters::http::ext_authz::v3::CheckSettings() /*check_settings_*/};
}

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
