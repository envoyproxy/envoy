#include "ext_authz.h"
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
using Filters::Common::MutationRules::CheckOperation;
using Filters::Common::MutationRules::CheckResult;

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

FilterConfig::FilterConfig(const envoy::extensions::filters::http::ext_authz::v3::ExtAuthz& config,
                           Stats::Scope& scope, const std::string& stats_prefix,
                           Server::Configuration::ServerFactoryContext& factory_context)
    : allow_partial_message_(config.with_request_body().allow_partial_message()),
      failure_mode_allow_(config.failure_mode_allow()),
      failure_mode_allow_header_add_(config.failure_mode_allow_header_add()),
      clear_route_cache_(config.clear_route_cache()),
      max_request_bytes_(config.with_request_body().max_request_bytes()),

      // `pack_as_bytes_` should be true when configured with the HTTP service because there is no
      // difference to where the body is written in http requests, and a value of false here will
      // cause non UTF-8 body content to be changed when it doesn't need to.
      pack_as_bytes_(config.has_http_service() || config.with_request_body().pack_as_bytes()),

      encode_raw_headers_(config.encode_raw_headers()),

      status_on_error_(toErrorCode(config.status_on_error().code())),
      validate_mutations_(config.validate_mutations()), scope_(scope),
      decoder_header_mutation_checker_(
          config.has_decoder_header_mutation_rules()
              ? absl::optional<Filters::Common::MutationRules::Checker>(
                    Filters::Common::MutationRules::Checker(config.decoder_header_mutation_rules(),
                                                            factory_context.regexEngine()))
              : absl::nullopt),
      enable_dynamic_metadata_ingestion_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, enable_dynamic_metadata_ingestion, true)),
      runtime_(factory_context.runtime()), http_context_(factory_context.httpContext()),
      filter_metadata_(config.has_filter_metadata() ? absl::optional(config.filter_metadata())
                                                    : absl::nullopt),
      emit_filter_state_stats_(config.emit_filter_state_stats()),
      filter_enabled_(config.has_filter_enabled()
                          ? absl::optional<Runtime::FractionalPercent>(
                                Runtime::FractionalPercent(config.filter_enabled(), runtime_))
                          : absl::nullopt),
      filter_enabled_metadata_(
          config.has_filter_enabled_metadata()
              ? absl::optional<Matchers::MetadataMatcher>(
                    Matchers::MetadataMatcher(config.filter_enabled_metadata(), factory_context))
              : absl::nullopt),
      deny_at_disable_(config.has_deny_at_disable()
                           ? absl::optional<Runtime::FeatureFlag>(
                                 Runtime::FeatureFlag(config.deny_at_disable(), runtime_))
                           : absl::nullopt),
      pool_(scope_.symbolTable()),
      metadata_context_namespaces_(config.metadata_context_namespaces().begin(),
                                   config.metadata_context_namespaces().end()),
      typed_metadata_context_namespaces_(config.typed_metadata_context_namespaces().begin(),
                                         config.typed_metadata_context_namespaces().end()),
      route_metadata_context_namespaces_(config.route_metadata_context_namespaces().begin(),
                                         config.route_metadata_context_namespaces().end()),
      route_typed_metadata_context_namespaces_(
          config.route_typed_metadata_context_namespaces().begin(),
          config.route_typed_metadata_context_namespaces().end()),
      include_peer_certificate_(config.include_peer_certificate()),
      include_tls_session_(config.include_tls_session()),
      charge_cluster_response_stats_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, charge_cluster_response_stats, true)),
      stats_(generateStats(stats_prefix, config.stat_prefix(), scope)),
      ext_authz_ok_(pool_.add(createPoolStatName(config.stat_prefix(), "ok"))),
      ext_authz_denied_(pool_.add(createPoolStatName(config.stat_prefix(), "denied"))),
      ext_authz_error_(pool_.add(createPoolStatName(config.stat_prefix(), "error"))),
      ext_authz_invalid_(pool_.add(createPoolStatName(config.stat_prefix(), "invalid"))),
      ext_authz_failure_mode_allowed_(
          pool_.add(createPoolStatName(config.stat_prefix(), "failure_mode_allowed"))) {
  auto bootstrap = factory_context.bootstrap();
  auto labels_key_it =
      bootstrap.node().metadata().fields().find(config.bootstrap_metadata_labels_key());
  if (labels_key_it != bootstrap.node().metadata().fields().end()) {
    for (const auto& labels_it : labels_key_it->second.struct_value().fields()) {
      destination_labels_[labels_it.first] = labels_it.second.string_value();
    }
  }

  if (config.has_allowed_headers() &&
      config.http_service().authorization_request().has_allowed_headers()) {
    throw EnvoyException("Invalid duplicate configuration for allowed_headers.");
  }

  // An unset request_headers_matchers_ means that all client request headers are allowed through
  // to the authz server; this is to preserve backwards compatibility when introducing
  // allowlisting of request headers for gRPC authz servers. Pre-existing support is for
  // HTTP authz servers only and defaults to blocking all but a few headers (i.e. Authorization,
  // Method, Path and Host).
  if (config.has_grpc_service() && config.has_allowed_headers()) {
    allowed_headers_matcher_ = Filters::Common::ExtAuthz::CheckRequestUtils::toRequestMatchers(
        config.allowed_headers(), false, factory_context);
  } else if (config.has_http_service()) {
    if (config.http_service().authorization_request().has_allowed_headers()) {
      allowed_headers_matcher_ = Filters::Common::ExtAuthz::CheckRequestUtils::toRequestMatchers(
          config.http_service().authorization_request().allowed_headers(), true, factory_context);
    } else {
      allowed_headers_matcher_ = Filters::Common::ExtAuthz::CheckRequestUtils::toRequestMatchers(
          config.allowed_headers(), true, factory_context);
    }
  }
  if (config.has_disallowed_headers()) {
    disallowed_headers_matcher_ = Filters::Common::ExtAuthz::CheckRequestUtils::toRequestMatchers(
        config.disallowed_headers(), false, factory_context);
  }
}

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

  // Now that we'll definitely be making the request, add filter state stats if configured to do so.
  const Envoy::StreamInfo::FilterStateSharedPtr& filter_state =
      decoder_callbacks_->streamInfo().filterState();
  if ((config_->emitFilterStateStats() || config_->filterMetadata().has_value())) {
    if (!filter_state->hasData<ExtAuthzLoggingInfo>(decoder_callbacks_->filterConfigName())) {
      filter_state->setData(decoder_callbacks_->filterConfigName(),
                            std::make_shared<ExtAuthzLoggingInfo>(config_->filterMetadata()),
                            Envoy::StreamInfo::FilterState::StateType::Mutable,
                            Envoy::StreamInfo::FilterState::LifeSpan::Request);

      // This may return nullptr (if there's a value at this name whose type doesn't match or isn't
      // mutable, for example), so we must check logging_info_ is not nullptr later.
      logging_info_ =
          filter_state->getDataMutable<ExtAuthzLoggingInfo>(decoder_callbacks_->filterConfigName());
    }
    if (logging_info_ == nullptr) {
      stats_.filter_state_name_collision_.inc();
      ENVOY_STREAM_LOG(debug,
                       "Could not find logging info at {}! (Did another filter already put data "
                       "at this name?)",
                       *decoder_callbacks_, decoder_callbacks_->filterConfigName());
    }
  }

  absl::optional<FilterConfigPerRoute> maybe_merged_per_route_config;
  for (const FilterConfigPerRoute& cfg :
       Http::Utility::getAllPerFilterConfig<FilterConfigPerRoute>(decoder_callbacks_)) {
    if (maybe_merged_per_route_config.has_value()) {
      maybe_merged_per_route_config.value().merge(cfg);
    } else {
      maybe_merged_per_route_config = cfg;
    }
  }

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
      std::move(route_metadata_context), check_request_, max_request_bytes_, config_->packAsBytes(),
      config_->headersAsBytes(), config_->includePeerCertificate(), config_->includeTLSSession(),
      config_->destinationLabels(), config_->allowedHeadersMatcher(),
      config_->disallowedHeadersMatcher());

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
          StreamInfo::CoreResponseFlag::UnauthorizedExternalService);
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

  max_request_bytes_ = config_->maxRequestBytes();
  if (buffer_data_) {
    ENVOY_STREAM_LOG(debug, "ext_authz filter is buffering the request", *decoder_callbacks_);

    allow_partial_message_ = check_settings.has_with_request_body()
                                 ? check_settings.with_request_body().allow_partial_message()
                                 : config_->allowPartialMessage();
    if (check_settings.has_with_request_body()) {
      max_request_bytes_ = check_settings.with_request_body().max_request_bytes();
    }
    if (!allow_partial_message_) {
      decoder_callbacks_->setDecoderBufferLimit(max_request_bytes_);
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
    for (const auto& [key, value] : response_headers_to_add_) {
      ENVOY_STREAM_LOG(trace, "'{}':'{}'", *encoder_callbacks_, key.get(), value);
      headers.addCopy(key, value);
    }
  }

  if (!response_headers_to_set_.empty()) {
    ENVOY_STREAM_LOG(
        trace, "ext_authz filter set header(s) to the encoded response:", *encoder_callbacks_);
    for (const auto& [key, value] : response_headers_to_set_) {
      ENVOY_STREAM_LOG(trace, "'{}':'{}'", *encoder_callbacks_, key.get(), value);
      headers.setCopy(key, value);
    }
  }

  if (!response_headers_to_add_if_absent_.empty()) {
    for (const auto& [key, value] : response_headers_to_add_if_absent_) {
      ENVOY_STREAM_LOG(trace, "'{}':'{}'", *encoder_callbacks_, key.get(), value);
      if (auto header_entry = headers.get(key); header_entry.empty()) {
        ENVOY_STREAM_LOG(trace, "ext_authz filter added header(s) to the encoded response:",
                         *encoder_callbacks_);
        headers.addCopy(key, value);
      }
    }
  }

  if (!response_headers_to_overwrite_if_exists_.empty()) {
    for (const auto& [key, value] : response_headers_to_overwrite_if_exists_) {
      ENVOY_STREAM_LOG(trace, "'{}':'{}'", *encoder_callbacks_, key, value);
      if (auto header_entry = headers.get(key); !header_entry.empty()) {
        ENVOY_STREAM_LOG(
            trace, "ext_authz filter set header(s) to the encoded response:", *encoder_callbacks_);
        headers.setCopy(key, value);
      }
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

void Filter::updateLoggingInfo() {
  if (!config_->emitFilterStateStats()) {
    return;
  }

  if (logging_info_ == nullptr) {
    return;
  }

  // Latency is the only stat available if we aren't using envoy grpc.
  if (start_time_.has_value()) {
    logging_info_->setLatency(std::chrono::duration_cast<std::chrono::microseconds>(
        decoder_callbacks_->dispatcher().timeSource().monotonicTime() - start_time_.value()));
  }

  auto const* stream_info = client_->streamInfo();
  if (stream_info == nullptr) {
    return;
  }

  const auto& bytes_meter = stream_info->getUpstreamBytesMeter();
  if (bytes_meter != nullptr) {
    logging_info_->setBytesSent(bytes_meter->wireBytesSent());
    logging_info_->setBytesReceived(bytes_meter->wireBytesReceived());
  }
  if (stream_info->upstreamInfo().has_value()) {
    logging_info_->setUpstreamHost(stream_info->upstreamInfo()->upstreamHost());
  }
  absl::optional<Upstream::ClusterInfoConstSharedPtr> cluster_info =
      stream_info->upstreamClusterInfo();
  if (cluster_info) {
    logging_info_->setClusterInfo(std::move(*cluster_info));
  }
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

CheckResult Filter::validateAndCheckDecoderHeaderMutation(
    Filters::Common::MutationRules::CheckOperation operation, absl::string_view key,
    absl::string_view value) const {
  if (config_->validateMutations() && (!Http::HeaderUtility::headerNameIsValid(key) ||
                                       !Http::HeaderUtility::headerValueIsValid(value))) {
    return CheckResult::FAIL;
  }
  // Check header mutation is valid according to configured decoder mutation rules.
  return config_->checkDecoderHeaderMutation(operation, Http::LowerCaseString(key), value);
}

void Filter::onComplete(Filters::Common::ExtAuthz::ResponsePtr&& response) {
  state_ = State::Complete;
  using Filters::Common::ExtAuthz::CheckStatus;
  Stats::StatName empty_stat_name;

  updateLoggingInfo();

  if (!response->dynamic_metadata.fields().empty()) {
    if (!config_->enableDynamicMetadataIngestion()) {
      ENVOY_STREAM_LOG(trace,
                       "Response is trying to inject dynamic metadata, but dynamic metadata "
                       "ingestion is disabled. Ignoring...",
                       *decoder_callbacks_);
      stats_.ignored_dynamic_metadata_.inc();
    } else {
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
    for (const auto& [key, value] : response->headers_to_set) {
      CheckResult check_result = validateAndCheckDecoderHeaderMutation(
          Filters::Common::MutationRules::CheckOperation::SET, key, value);
      switch (check_result) {
      case CheckResult::OK:
        ENVOY_STREAM_LOG(trace, "'{}':'{}'", *decoder_callbacks_, key, value);
        request_headers_->setCopy(Http::LowerCaseString(key), value);
        break;
      case CheckResult::IGNORE:
        ENVOY_STREAM_LOG(trace, "Ignoring invalid header to set '{}':'{}'.", *decoder_callbacks_,
                         key, value);
        break;
      case CheckResult::FAIL:
        ENVOY_STREAM_LOG(trace, "Rejecting invalid header to set '{}':'{}'.", *decoder_callbacks_,
                         key, value);
        rejectResponse();
        return;
      }
    }
    for (const auto& [key, value] : response->headers_to_add) {
      CheckResult check_result = validateAndCheckDecoderHeaderMutation(
          Filters::Common::MutationRules::CheckOperation::SET, key, value);
      switch (check_result) {
      case CheckResult::OK:
        ENVOY_STREAM_LOG(trace, "'{}':'{}'", *decoder_callbacks_, key, value);
        request_headers_->addCopy(Http::LowerCaseString(key), value);
        break;
      case CheckResult::IGNORE:
        ENVOY_STREAM_LOG(trace, "Ignoring invalid header to add '{}':'{}'.", *decoder_callbacks_,
                         key, value);
        break;
      case CheckResult::FAIL:
        ENVOY_STREAM_LOG(trace, "Rejecting invalid header to add '{}':'{}'.", *decoder_callbacks_,
                         key, value);
        rejectResponse();
        return;
      }
    }
    for (const auto& [key, value] : response->headers_to_append) {
      CheckResult check_result = validateAndCheckDecoderHeaderMutation(
          Filters::Common::MutationRules::CheckOperation::APPEND, key, value);
      switch (check_result) {
      case CheckResult::OK: {
        ENVOY_STREAM_LOG(trace, "'{}':'{}'", *decoder_callbacks_, key, value);
        Http::LowerCaseString lowercase_key(key);
        const auto header_to_modify = request_headers_->get(lowercase_key);
        // TODO(dio): Add a flag to allow appending non-existent headers, without setting it
        // first (via `headers_to_add`). For example, given:
        // 1. Original headers {"original": "true"}
        // 2. Response headers from the authorization servers {{"append": "1"}, {"append":
        // "2"}}
        //
        // Currently it is not possible to add {{"append": "1"}, {"append": "2"}} (the
        // intended combined headers: {{"original": "true"}, {"append": "1"}, {"append":
        // "2"}}) to the request to upstream server by only sets `headers_to_append`.
        if (!header_to_modify.empty()) {
          ENVOY_STREAM_LOG(trace, "'{}':'{}'", *decoder_callbacks_, key, value);
          // The current behavior of appending is by combining entries with the same key,
          // into one entry. The value of that combined entry is separated by ",".
          // TODO(dio): Consider to use addCopy instead.
          request_headers_->appendCopy(lowercase_key, value);
        }
        break;
      }
      case CheckResult::IGNORE:
        ENVOY_STREAM_LOG(trace, "Ignoring invalid header to append '{}':'{}'.", *decoder_callbacks_,
                         key, value);
        break;
      case CheckResult::FAIL:
        ENVOY_STREAM_LOG(trace, "Rejecting invalid header to append '{}':'{}'.",
                         *decoder_callbacks_, key, value);
        rejectResponse();
        return;
      }
    }

    ENVOY_STREAM_LOG(trace,
                     "ext_authz filter removed header(s) from the request:", *decoder_callbacks_);
    for (const auto& key : response->headers_to_remove) {
      // If the response contains an invalid header to remove, it's the same as trying to remove a
      // header that doesn't exist, so just ignore it.
      if (config_->validateMutations() && !Http::HeaderUtility::headerNameIsValid(key)) {
        ENVOY_STREAM_LOG(trace, "Ignoring invalid header removal '{}'.", *decoder_callbacks_, key);
        continue;
      }
      // We don't allow removing any :-prefixed headers, nor Host, as removing them would make the
      // request malformed. checkDecoderHeaderMutation also performs this check, however, so only
      // perform this check explicitly if decoder header mutation rules is empty.
      if (!config_->hasDecoderHeaderMutationRules() &&
          !Http::HeaderUtility::isRemovableHeader(key)) {
        ENVOY_STREAM_LOG(trace, "Ignoring invalid header removal '{}'.", *decoder_callbacks_, key);
        continue;
      }
      // Check header mutation is valid according to configured decoder header mutation rules.
      Http::LowerCaseString lowercase_key(key);
      switch (config_->checkDecoderHeaderMutation(CheckOperation::REMOVE, lowercase_key,
                                                  EMPTY_STRING)) {
      case CheckResult::OK:
        ENVOY_STREAM_LOG(trace, "'{}'", *decoder_callbacks_, key);
        request_headers_->remove(lowercase_key);
        break;
      case CheckResult::IGNORE:
        ENVOY_STREAM_LOG(trace, "Ignoring disallowed header removal '{}'.", *decoder_callbacks_,
                         key);
        break;
      case CheckResult::FAIL:
        ENVOY_STREAM_LOG(trace, "Rejecting disallowed header removal '{}'.", *decoder_callbacks_,
                         key);
        rejectResponse();
        return;
      }
    }

    if (!response->response_headers_to_add.empty()) {
      ENVOY_STREAM_LOG(trace, "ext_authz filter saving {} header(s) to add to the response:",
                       *decoder_callbacks_, response->response_headers_to_add.size());
      for (const auto& [key, value] : response->response_headers_to_add) {
        if (config_->validateMutations() && (!Http::HeaderUtility::headerNameIsValid(key) ||
                                             !Http::HeaderUtility::headerValueIsValid(value))) {
          ENVOY_STREAM_LOG(trace, "Rejected invalid header '{}':'{}'.", *decoder_callbacks_, key,
                           value);
          rejectResponse();
          return;
        }
      }
      response_headers_to_add_ = Http::HeaderVector{response->response_headers_to_add.begin(),
                                                    response->response_headers_to_add.end()};
    }

    if (!response->response_headers_to_set.empty()) {
      ENVOY_STREAM_LOG(trace, "ext_authz filter saving {} header(s) to set to the response:",
                       *decoder_callbacks_, response->response_headers_to_set.size());
      for (const auto& [key, value] : response->response_headers_to_set) {
        if (config_->validateMutations() && (!Http::HeaderUtility::headerNameIsValid(key) ||
                                             !Http::HeaderUtility::headerValueIsValid(value))) {
          ENVOY_STREAM_LOG(trace, "Rejected invalid header '{}':'{}'.", *decoder_callbacks_, key,
                           value);
          rejectResponse();
          return;
        }
      }
      response_headers_to_set_ = Http::HeaderVector{response->response_headers_to_set.begin(),
                                                    response->response_headers_to_set.end()};
    }

    if (!response->response_headers_to_add_if_absent.empty()) {
      ENVOY_STREAM_LOG(trace, "ext_authz filter saving {} header(s) to add to the response:",
                       *decoder_callbacks_, response->response_headers_to_add_if_absent.size());
      for (const auto& [key, value] : response->response_headers_to_add_if_absent) {
        if (config_->validateMutations() && (!Http::HeaderUtility::headerNameIsValid(key) ||
                                             !Http::HeaderUtility::headerValueIsValid(value))) {
          ENVOY_STREAM_LOG(trace, "Rejected invalid header '{}':'{}'.", *decoder_callbacks_, key,
                           value);
          rejectResponse();
          return;
        }
      }
      response_headers_to_add_if_absent_ =
          Http::HeaderVector{response->response_headers_to_add_if_absent.begin(),
                             response->response_headers_to_add_if_absent.end()};
    }

    if (!response->response_headers_to_overwrite_if_exists.empty()) {
      ENVOY_STREAM_LOG(trace, "ext_authz filter saving {} header(s) to set to the response:",
                       *decoder_callbacks_,
                       response->response_headers_to_overwrite_if_exists.size());
      for (const auto& [key, value] : response->response_headers_to_overwrite_if_exists) {
        if (config_->validateMutations() && (!Http::HeaderUtility::headerNameIsValid(key) ||
                                             !Http::HeaderUtility::headerValueIsValid(value))) {
          ENVOY_STREAM_LOG(trace, "Rejected invalid header '{}':'{}'.", *decoder_callbacks_, key,
                           value);
          rejectResponse();
          return;
        }
      }
      response_headers_to_overwrite_if_exists_ =
          Http::HeaderVector{response->response_headers_to_overwrite_if_exists.begin(),
                             response->response_headers_to_overwrite_if_exists.end()};
    }

    absl::optional<Http::Utility::QueryParamsMulti> modified_query_parameters;
    if (!response->query_parameters_to_set.empty()) {
      modified_query_parameters = Http::Utility::QueryParamsMulti::parseQueryString(
          request_headers_->Path()->value().getStringView());
      ENVOY_STREAM_LOG(
          trace, "ext_authz filter set query parameter(s) on the request:", *decoder_callbacks_);
      for (const auto& [key, value] : response->query_parameters_to_set) {
        if (config_->validateMutations() &&
            (!Http::Utility::PercentEncoding::queryParameterIsUrlEncoded(key) ||
             !Http::Utility::PercentEncoding::queryParameterIsUrlEncoded(value))) {
          ENVOY_STREAM_LOG(trace, "Rejected invalid query parameter {}={}.", *decoder_callbacks_,
                           key, value);
          rejectResponse();
          return;
        }
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

    // Check headers are valid.
    if (config_->validateMutations()) {
      for (const auto& [key, value] : response->headers_to_set) {
        if (!Http::HeaderUtility::headerNameIsValid(key) ||
            !Http::HeaderUtility::headerValueIsValid(value)) {
          ENVOY_STREAM_LOG(trace, "Rejected invalid header '{}':'{}'.", *decoder_callbacks_, key,
                           value);
          rejectResponse();
          return;
        }
      }
    }

    // setResponseFlag must be called before sendLocalReply
    decoder_callbacks_->streamInfo().setResponseFlag(
        StreamInfo::CoreResponseFlag::UnauthorizedExternalService);
    decoder_callbacks_->sendLocalReply(
        response->status_code, response->body,
        [&headers = response->headers_to_set,
         &callbacks = *decoder_callbacks_](Http::HeaderMap& response_headers) -> void {
          ENVOY_STREAM_LOG(trace,
                           "ext_authz filter added header(s) to the local response:", callbacks);
          // Firstly, remove all headers requested by the ext_authz filter, to ensure that they will
          // override existing headers.
          for (const auto& [key, _] : headers) {
            response_headers.remove(Http::LowerCaseString(key));
          }
          // Then set all of the requested headers, allowing the same header to be set multiple
          // times, e.g. `Set-Cookie`.
          for (const auto& [key, value] : headers) {
            ENVOY_STREAM_LOG(trace, " '{}':'{}'", callbacks, key, value);
            response_headers.addCopy(Http::LowerCaseString(key), value);
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
          StreamInfo::CoreResponseFlag::UnauthorizedExternalService);
      decoder_callbacks_->sendLocalReply(
          config_->statusOnError(), EMPTY_STRING, nullptr, absl::nullopt,
          Filters::Common::ExtAuthz::ResponseCodeDetails::get().AuthzError);
    }
    break;
  }
  }
}

void Filter::rejectResponse() {
  const Http::Code status = Http::Code::InternalServerError;
  ENVOY_STREAM_LOG(trace, "ext_authz filter invalidated the response. Response status code: {}",
                   *decoder_callbacks_, enumToInt(status));
  if (cluster_) {
    config_->incCounter(cluster_->statsScope(), config_->ext_authz_invalid_);
  }
  stats_.invalid_.inc();
  decoder_callbacks_->streamInfo().setResponseFlag(
      StreamInfo::CoreResponseFlag::UnauthorizedExternalService);
  decoder_callbacks_->sendLocalReply(
      status, EMPTY_STRING, nullptr, absl::nullopt,
      Filters::Common::ExtAuthz::ResponseCodeDetails::get().AuthzInvalid);
}

bool Filter::isBufferFull(uint64_t num_bytes_processing) const {
  if (!allow_partial_message_) {
    return false;
  }

  uint64_t num_bytes_buffered = num_bytes_processing;
  const auto* buffer = decoder_callbacks_->decodingBuffer();
  if (buffer != nullptr) {
    num_bytes_buffered += buffer->length();
  }

  return num_bytes_buffered >= max_request_bytes_;
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
