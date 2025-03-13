#include "source/extensions/filters/http/gcp_authn/gcp_authn_filter.h"

#include <memory>
#include <string>

#include "source/common/common/enum_to_int.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"

#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {
namespace {
void addTokenToRequest(Http::RequestHeaderMap& hdrs, absl::string_view token_str,
                       const envoy::extensions::filters::http::gcp_authn::v3::TokenHeader& header) {
  if (header.ByteSizeLong() == 0) {
    std::string id_token = absl::StrCat("Bearer ", token_str);
    hdrs.setCopy(authorizationHeaderKey(), id_token);
  } else {
    std::string id_token = absl::StrCat(header.value_prefix(), token_str);
    hdrs.setCopy(Http::LowerCaseString(header.name()), id_token);
  }
}
} // namespace

using ::Envoy::Router::RouteConstSharedPtr;
using ::google::jwt_verify::Status;
using Http::FilterHeadersStatus;

// TODO(tyxia) Handle the duplicated outstanding requests.
Http::FilterHeadersStatus GcpAuthnFilter::decodeHeaders(Http::RequestHeaderMap& hdrs, bool) {
  Envoy::Router::RouteConstSharedPtr route = decoder_callbacks_->route();
  if (route == nullptr || route->routeEntry() == nullptr) {
    // Nothing to do if no route, continue the filter chain iteration.
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  state_ = State::Calling;
  initiating_call_ = true;

  Envoy::Upstream::ThreadLocalCluster* cluster =
      context_.serverFactoryContext().clusterManager().getThreadLocalCluster(
          route->routeEntry()->clusterName());

  if (cluster != nullptr) {
    // The `audience` is passed to filter through cluster metadata.
    auto filter_metadata = cluster->info()->metadata().typed_filter_metadata();
    const auto filter_it = filter_metadata.find(std::string(FilterName));
    if (filter_it != filter_metadata.end()) {
      envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
      THROW_IF_NOT_OK(MessageUtil::unpackTo(filter_it->second, audience));
      audience_str_ = audience.url();
    }
  }

  if (!audience_str_.empty()) {
    if (jwt_token_cache_ != nullptr) {
      auto token = jwt_token_cache_->lookUp(audience_str_);
      if (token != nullptr) {
        // If token is found in the cache, we add the token string to the request directly and
        // continue the filter chain iteration.
        addTokenToRequest(hdrs, token->jwt_, filter_config_->token_header());
        return FilterHeadersStatus::Continue;
      }
    }

    // Save the pointer to the request headers for header manipulation based on http response later.
    request_header_map_ = &hdrs;
    // Audience is URL of receiving service that will perform authentication.
    // The URL format is
    // "http://metadata.google.internal/computeMetadata/v1/instance/service-accounts/default/identity?audience=[AUDIENCE]"
    // So, we add the audience from the config to the final url by substituting the `[AUDIENCE]`
    // with real audience string from the config.

    std::string final_url = absl::StrReplaceAll(
        Runtime::runtimeFeatureEnabled("envoy.reloadable_features.gcp_authn_use_fixed_url")
            ? UrlString
            : filter_config_->http_uri().uri(),
        {{"[AUDIENCE]", audience_str_}});
    client_->fetchToken(*this, buildRequest(final_url));
    initiating_call_ = false;
  } else {
    // There is no need to fetch the token if no audience is specified because no
    // authentication will be performed. So, we just continue the filter chain iteration.
    stats_.retrieve_audience_failed_.inc();
    state_ = State::Complete;
  }

  // Stop the iteration for headers as well as data and trailers for the current filter and the
  // filters following.
  return state_ == State::Complete ? FilterHeadersStatus::Continue
                                   : Http::FilterHeadersStatus::StopAllIterationAndWatermark;
}

void GcpAuthnFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

void GcpAuthnFilter::onComplete(const Http::ResponseMessage* response) {
  state_ = State::Complete;
  if (!initiating_call_) {
    if (response != nullptr) {
      // Modify the request header to include the ID token in a header (by default, the
      // `Authorization: Bearer ID_TOKEN` header).
      std::string token_str = response->bodyAsString();
      if (request_header_map_ != nullptr) {
        addTokenToRequest(*request_header_map_, token_str, filter_config_->token_header());
      } else {
        ENVOY_LOG(debug, "No request header to be modified.");
      }
      // Decode the tokens.
      std::unique_ptr<::google::jwt_verify::Jwt> jwt =
          std::make_unique<::google::jwt_verify::Jwt>();
      Status status = jwt->parseFromString(token_str);
      if (status == Status::Ok) {
        if (jwt_token_cache_ != nullptr) {
          // Insert the token into cache along with the ownership transfer.
          jwt_token_cache_->insert(audience_str_, std::move(jwt));
        }
      } else {
        ENVOY_LOG(error, "Failed to parse the token string, status : {}", Envoy::enumToInt(status));
      }
    }
    decoder_callbacks_->continueDecoding();
  }
}

void GcpAuthnFilter::onDestroy() {
  if (state_ == State::Calling) {
    state_ = State::Complete;
    client_->cancel();
  }
}

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
