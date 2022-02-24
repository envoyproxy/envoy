#include "source/extensions/filters/http/gcp_authn/gcp_authn_filter.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthentication {

using Http::FilterHeadersStatus;

constexpr char kMetadataFlavorKey[] = "Metadata-Flavor";
constexpr char kMetadataFlavor[] = "Google";

Http::FilterHeadersStatus GcpAuthnFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  return FilterHeadersStatus::Continue;
}

Http::RequestMessagePtr GcpAuthnClient::buildRequest(const std::string& method) {
  absl::string_view host;
  absl::string_view path;
  absl::string_view token_url = config_.http_uri().uri();
  Envoy::Http::Utility::extractHostPathFromUri(token_url, host, path);
  Http::RequestHeaderMapPtr headers =
      Envoy::Http::createHeaderMap<Envoy::Http::RequestHeaderMapImpl>(
          {{Envoy::Http::Headers::get().Method, method},
           {Envoy::Http::Headers::get().Host, std::string(host)},
           {Envoy::Http::Headers::get().Path, std::string(path)},
           {Envoy::Http::LowerCaseString(kMetadataFlavorKey), kMetadataFlavor}});

  return std::make_unique<Envoy::Http::RequestMessageImpl>(std::move(headers));
}

} // namespace GcpAuthentication
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy