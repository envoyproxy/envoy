#include "source/extensions/filters/http/gcp_authn/gcp_authn_filter.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"

#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

using ::Envoy::Router::RouteConstSharedPtr;
using Http::FilterHeadersStatus;

Http::FilterHeadersStatus GcpAuthnFilter::decodeHeaders(Http::RequestHeaderMap& hdrs, bool) {
  Envoy::Router::RouteConstSharedPtr route = decoder_callbacks_->route();
  if (route == nullptr || route->routeEntry() == nullptr) {
    // Nothing to do if no route, continue the filter chain iteration.
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  state_ = State::Calling;
  initiating_call_ = true;

  Envoy::Upstream::ThreadLocalCluster* cluster =
      context_.clusterManager().getThreadLocalCluster(route->routeEntry()->clusterName());

  std::string audience_str;
  if (cluster != nullptr) {
    // The `audience` is passed to filter through cluster metadata.
    auto filter_metadata = cluster->info()->metadata().typed_filter_metadata();
    const auto filter_it = filter_metadata.find(std::string(FilterName));
    if (filter_it != filter_metadata.end()) {
      envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
      MessageUtil::unpackTo(filter_it->second, audience);
      auto it = audience.audience_map().find(std::string(AudienceKey));
      if (it != audience.audience_map().end()) {
        audience_str = it->second;
      }
    }
  }

  if (!audience_str.empty()) {
    // Save the pointer to the request headers for header manipulation based on http response later.
    request_header_map_ = &hdrs;
    // Audience is URL of receiving service that will perform authentication.
    // The URL format is
    // "http://metadata.google.internal/computeMetadata/v1/instance/service-accounts/default/identity?audience=[AUDIENCE]"
    // So, we add the audience from the config to the final url by substituting the `[AUDIENCE]`
    // with real audience string from the config.
    std::string final_url =
        absl::StrReplaceAll(filter_config_->http_uri().uri(), {{"[AUDIENCE]", audience_str}});
    client_->fetchToken(*this, buildRequest(final_url));
    initiating_call_ = false;
  } else {
    // There is no need to fetch the token if no audience is specified because no
    // authentication will be performed. So, we just continue the filter chain iteration.
    stats_.retrieve_audience_failed_.inc();
    state_ = State::Complete;
  }

  return state_ == State::Complete ? FilterHeadersStatus::Continue
                                   : Http::FilterHeadersStatus::StopIteration;
}

void GcpAuthnFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

void GcpAuthnFilter::onComplete(const Http::ResponseMessage* response) {
  state_ = State::Complete;
  if (!initiating_call_) {
    if (request_header_map_ == nullptr) {
      ENVOY_LOG(debug, "No request header to be modified.");
    } else {
      // Modify the request header to include the ID token in an `Authorization: Bearer ID_TOKEN`
      // header.
      std::string id_token = absl::StrCat("Bearer ", response->bodyAsString());
      request_header_map_->addCopy(authorizationHeaderKey(), id_token);
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
