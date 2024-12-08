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

void ParameterMutationAppend::mutateQueryParameter(
    Http::Utility::QueryParamsMulti& params, const Formatter::HttpFormatterContext& context,
    const StreamInfo::StreamInfo& stream_info) const {

  const std::string value = formatter_->formatWithContext(context, stream_info);

  switch (action_) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case ParameterAppendProto::APPEND_IF_EXISTS_OR_ADD:
    params.add(key_, value);
    return;
  case ParameterAppendProto::ADD_IF_ABSENT: {
    auto iter = params.data().find(key_);
    if (iter == params.data().end()) {
      params.add(key_, value);
    }
    break;
  }
  case ParameterAppendProto::OVERWRITE_IF_EXISTS: {
    auto iter = params.data().find(key_);
    if (iter != params.data().end()) {
      params.overwrite(key_, value);
    }
    break;
  }
  case ParameterAppendProto::OVERWRITE_IF_EXISTS_OR_ADD:
    params.overwrite(key_, value);
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

  for (const auto& mutation : config.parameter_mutations()) {
    if (mutation.has_append()) {
      if (!mutation.append().has_record()) {
        creation_status = absl::InvalidArgumentError("No record specified for parameter mutation.");
        return;
      }
      if (!mutation.append().record().value().has_string_value()) {
        creation_status = absl::InvalidArgumentError("Only string value is allowed for parameter.");
        return;
      }

      auto value_or_error =
          Formatter::FormatterImpl::create(mutation.append().record().value().string_value(), true);
      SET_AND_RETURN_IF_NOT_OK(value_or_error.status(), creation_status);
      parameter_mutations_.emplace_back(std::make_unique<ParameterMutationAppend>(
          mutation.append().record().key(), std::move(value_or_error.value()),
          mutation.append().action()));

    } else if (!mutation.remove().empty()) {
      parameter_mutations_.emplace_back(
          std::make_unique<ParameterMutationRemove>(mutation.remove()));
    } else {
      creation_status = absl::InvalidArgumentError("No mutation specified.");
      return;
    }
  }
}

void Mutations::mutateRequestHeaders(Http::RequestHeaderMap& headers,
                                     const Formatter::HttpFormatterContext& context,
                                     const StreamInfo::StreamInfo& stream_info) const {
  request_mutations_->evaluateHeaders(headers, context, stream_info);

  if (parameter_mutations_.empty() || headers.Path() == nullptr) {
    return;
  }

  // Mutate query parameters.
  Http::Utility::QueryParamsMulti params =
      Http::Utility::QueryParamsMulti::parseAndDecodeQueryString(headers.getPathValue());
  for (const auto& mutation : parameter_mutations_) {
    mutation->mutateQueryParameter(params, context, stream_info);
  }
  headers.setPath(params.replaceQueryString(headers.Path()->value()));
}

void Mutations::mutateResponseHeaders(Http::ResponseHeaderMap& headers,
                                      const Formatter::HttpFormatterContext& context,
                                      const StreamInfo::StreamInfo& stream_info) const {
  response_mutations_->evaluateHeaders(headers, context, stream_info);
}

PerRouteHeaderMutation::PerRouteHeaderMutation(const PerRouteProtoConfig& config,
                                               absl::Status& creation_status)
    : mutations_(config.mutations(), creation_status) {}

HeaderMutationConfig::HeaderMutationConfig(const ProtoConfig& config, absl::Status& creation_status)
    : mutations_(config.mutations(), creation_status),
      most_specific_header_mutations_wins_(config.most_specific_header_mutations_wins()) {}

void HeaderMutation::initializeRouteConfigs(Http::StreamFilterCallbacks* callbacks) {
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

  initializeRouteConfigs(decoder_callbacks_);

  for (const PerRouteHeaderMutation& route_config : route_configs_) {
    route_config.mutations().mutateRequestHeaders(headers, context,
                                                  decoder_callbacks_->streamInfo());
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus HeaderMutation::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  Formatter::HttpFormatterContext context{encoder_callbacks_->requestHeaders().ptr(), &headers};
  config_->mutations().mutateResponseHeaders(headers, context, encoder_callbacks_->streamInfo());

  // If we haven't already traversed the route configs, do so now.
  if (!route_configs_initialized_) {
    initializeRouteConfigs(encoder_callbacks_);
  }

  for (const PerRouteHeaderMutation& route_config : route_configs_) {
    route_config.mutations().mutateResponseHeaders(headers, context,
                                                   encoder_callbacks_->streamInfo());
  }

  return Http::FilterHeadersStatus::Continue;
}

} // namespace HeaderMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
