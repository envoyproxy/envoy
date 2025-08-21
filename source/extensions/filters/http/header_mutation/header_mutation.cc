#include "source/extensions/filters/http/header_mutation/header_mutation.h"

#include <cstdint>
#include <memory>

#include "source/common/config/utility.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderMutation {

void QueryParameterMutationAppend::mutateQueryParameter(
    Http::Utility::QueryParamsMulti& params, const Formatter::HttpFormatterContext& context,
    const StreamInfo::StreamInfo& stream_info) const {

  switch (action_) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case ParameterAppendProto::APPEND_IF_EXISTS_OR_ADD:
    params.add(key_, formatter_->formatWithContext(context, stream_info));
    return;
  case ParameterAppendProto::ADD_IF_ABSENT: {
    auto iter = params.data().find(key_);
    if (iter == params.data().end()) {
      params.add(key_, formatter_->formatWithContext(context, stream_info));
    }
    break;
  }
  case ParameterAppendProto::OVERWRITE_IF_EXISTS: {
    auto iter = params.data().find(key_);
    if (iter != params.data().end()) {
      params.overwrite(key_, formatter_->formatWithContext(context, stream_info));
    }
    break;
  }
  case ParameterAppendProto::OVERWRITE_IF_EXISTS_OR_ADD:
    params.overwrite(key_, formatter_->formatWithContext(context, stream_info));
    break;
  }
}

Mutations::Mutations(const MutationsProto& config, absl::Status& creation_status) {
  auto request_mutations_or_error = HeaderMutations::create(config.request_mutations());
  SET_AND_RETURN_IF_NOT_OK(request_mutations_or_error.status(), creation_status);
  request_mutations_ = std::move(request_mutations_or_error.value());

  auto response_mutations_or_error = HeaderMutations::create(config.response_mutations());
  SET_AND_RETURN_IF_NOT_OK(response_mutations_or_error.status(), creation_status);
  response_mutations_ = std::move(response_mutations_or_error.value());

  auto response_trailers_mutations_or_error =
      HeaderMutations::create(config.response_trailers_mutations());
  SET_AND_RETURN_IF_NOT_OK(response_trailers_mutations_or_error.status(), creation_status);
  response_trailers_mutations_ = std::move(response_trailers_mutations_or_error.value());

  auto request_trailers_mutations_or_error =
      HeaderMutations::create(config.request_trailers_mutations());
  SET_AND_RETURN_IF_NOT_OK(request_trailers_mutations_or_error.status(), creation_status);
  request_trailers_mutations_ = std::move(request_trailers_mutations_or_error.value());

  query_query_parameter_mutations_.reserve(config.query_parameter_mutations_size());
  for (const auto& mutation : config.query_parameter_mutations()) {
    if (mutation.has_append()) {
      if (!mutation.remove().empty()) {
        creation_status =
            absl::InvalidArgumentError("Only one of 'append'/'remove can be specified.");
        return;
      }

      if (!mutation.append().has_record()) {
        creation_status = absl::InvalidArgumentError("No record specified for append mutation.");
        return;
      }
      if (!mutation.append().record().value().has_string_value()) {
        creation_status =
            absl::InvalidArgumentError("Only string value is allowed for record value.");
        return;
      }

      auto value_or_error =
          Formatter::FormatterImpl::create(mutation.append().record().value().string_value(), true);
      SET_AND_RETURN_IF_NOT_OK(value_or_error.status(), creation_status);
      query_query_parameter_mutations_.emplace_back(std::make_unique<QueryParameterMutationAppend>(
          mutation.append().record().key(), std::move(value_or_error.value()),
          mutation.append().action()));

    } else if (!mutation.remove().empty()) {
      query_query_parameter_mutations_.emplace_back(
          std::make_unique<QueryParameterMutationRemove>(mutation.remove()));
    } else {
      creation_status = absl::InvalidArgumentError("One of 'append'/'remove' must be specified.");
      return;
    }
  }
}

void Mutations::mutateRequestHeaders(Http::RequestHeaderMap& headers,
                                     const Formatter::HttpFormatterContext& context,
                                     const StreamInfo::StreamInfo& stream_info) const {
  request_mutations_->evaluateHeaders(headers, context, stream_info);

  if (query_query_parameter_mutations_.empty() || headers.Path() == nullptr) {
    return;
  }

  // Mutate query parameters.
  Http::Utility::QueryParamsMulti params =
      Http::Utility::QueryParamsMulti::parseAndDecodeQueryString(headers.getPathValue());
  for (const auto& mutation : query_query_parameter_mutations_) {
    mutation->mutateQueryParameter(params, context, stream_info);
  }
  headers.setPath(params.replaceQueryString(headers.Path()->value()));
}

void Mutations::mutateResponseHeaders(Http::ResponseHeaderMap& headers,
                                      const Formatter::HttpFormatterContext& context,
                                      const StreamInfo::StreamInfo& stream_info) const {
  response_mutations_->evaluateHeaders(headers, context, stream_info);
}

void Mutations::mutateResponseTrailers(Http::ResponseTrailerMap& trailers,
                                       const Formatter::HttpFormatterContext& context,
                                       const StreamInfo::StreamInfo& stream_info) const {
  response_trailers_mutations_->evaluateHeaders(trailers, context, stream_info);
}

void Mutations::mutateRequestTrailers(Http::RequestTrailerMap& trailers,
                                      const Formatter::HttpFormatterContext& context,
                                      const StreamInfo::StreamInfo& stream_info) const {
  request_trailers_mutations_->evaluateHeaders(trailers, context, stream_info);
}

PerRouteHeaderMutation::PerRouteHeaderMutation(const PerRouteProtoConfig& config,
                                               absl::Status& creation_status)
    : mutations_(config.mutations(), creation_status) {}

HeaderMutationConfig::HeaderMutationConfig(const ProtoConfig& config, absl::Status& creation_status)
    : mutations_(config.mutations(), creation_status),
      most_specific_header_mutations_wins_(config.most_specific_header_mutations_wins()) {}

void HeaderMutation::maybeInitializeRouteConfigs(Http::StreamFilterCallbacks* callbacks) {
  // Ensure that route configs are initialized only once and the same route configs are used
  // for both decoding and encoding paths.
  // An independent flag is used to ensure even at the case where the route configs is empty,
  // we still won't try to initialize it again.
  if (route_configs_initialized_) {
    return;
  }
  route_configs_initialized_ = true;

  // Traverse through all route configs to retrieve all available header mutations.
  // `getAllPerFilterConfig` returns in ascending order of specificity (i.e., route table
  // first, then virtual host, then per route).
  route_configs_ = Http::Utility::getAllPerFilterConfig<PerRouteHeaderMutation>(callbacks);

  // The order of returned route configs is route table first, then virtual host, then
  // per route. That means the per route configs will be evaluated at the end and will
  // override the more general virtual host and route table configs.
  //
  // So, if most_specific_header_mutations_wins is false, we need to reverse the order
  // of route configs.
  if (!config_->mostSpecificHeaderMutationsWins()) {
    std::reverse(route_configs_.begin(), route_configs_.end());
  }
}

Http::FilterHeadersStatus HeaderMutation::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  Formatter::HttpFormatterContext context{&headers};
  config_->mutations().mutateRequestHeaders(headers, context, decoder_callbacks_->streamInfo());

  maybeInitializeRouteConfigs(decoder_callbacks_);

  for (const PerRouteHeaderMutation& route_config : route_configs_) {
    route_config.mutations().mutateRequestHeaders(headers, context,
                                                  decoder_callbacks_->streamInfo());
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus HeaderMutation::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  Formatter::HttpFormatterContext context{encoder_callbacks_->requestHeaders().ptr(), &headers};
  config_->mutations().mutateResponseHeaders(headers, context, encoder_callbacks_->streamInfo());

  // Note if the filter before this one has send local reply then the decodeHeaders() will not be
  // be called and the route config will not be initialized. Try it again here and it will be
  // no-op if already initialized.
  maybeInitializeRouteConfigs(encoder_callbacks_);

  for (const PerRouteHeaderMutation& route_config : route_configs_) {
    route_config.mutations().mutateResponseHeaders(headers, context,
                                                   encoder_callbacks_->streamInfo());
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterTrailersStatus HeaderMutation::encodeTrailers(Http::ResponseTrailerMap& trailers) {
  Formatter::HttpFormatterContext context{encoder_callbacks_->requestHeaders().ptr(),
                                          encoder_callbacks_->responseHeaders().ptr(), &trailers};
  config_->mutations().mutateResponseTrailers(trailers, context, encoder_callbacks_->streamInfo());

  maybeInitializeRouteConfigs(encoder_callbacks_);

  for (const PerRouteHeaderMutation& route_config : route_configs_) {
    route_config.mutations().mutateResponseTrailers(trailers, context,
                                                    encoder_callbacks_->streamInfo());
  }
  return Http::FilterTrailersStatus::Continue;
}

Http::FilterTrailersStatus HeaderMutation::decodeTrailers(Http::RequestTrailerMap& trailers) {
  // TODO(davinci26): if `HttpFormatterContext` supports request trailers we can also pass the
  // trailers to the context so we can support substitutions from other trailers.
  Formatter::HttpFormatterContext context{encoder_callbacks_->requestHeaders().ptr()};
  config_->mutations().mutateRequestTrailers(trailers, context, encoder_callbacks_->streamInfo());

  maybeInitializeRouteConfigs(encoder_callbacks_);

  for (const PerRouteHeaderMutation& route_config : route_configs_) {
    route_config.mutations().mutateRequestTrailers(trailers, context,
                                                   encoder_callbacks_->streamInfo());
  }
  return Http::FilterTrailersStatus::Continue;
}

} // namespace HeaderMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
