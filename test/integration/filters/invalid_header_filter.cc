#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/http/header_utility.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

namespace Envoy {

// Faulty filter that may remove critical headers.
class InvalidHeaderFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "invalid-header-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override {
    // Remove method when there is a "remove-method" header.
    if (!headers.get(Http::LowerCaseString("remove-method")).empty()) {
      headers.removeMethod();
    }
    if (!headers.get(Http::LowerCaseString("remove-path")).empty()) {
      headers.removePath();
    }

    if (!headers.get(Http::LowerCaseString("x-add-invalid-header-key")).empty()) {
      // Insert invalid characters by bypassing assert(valid()) checks, which would not run
      // in release mode
      headers.addCopy(Http::LowerCaseString("x-foo"), "hello  x-oops: yes");
      absl::string_view value =
          headers.get(Http::LowerCaseString("x-foo"))[0]->value().getStringView();
      char* data = const_cast<char*>(value.data());
      data[5] = '\r';
      data[6] = '\n';
    }

    if (!headers.get(Http::LowerCaseString("x-add-invalid-header-value")).empty()) {
      // Insert invalid characters by bypassing assert(valid()) checks, which would not run
      // in release mode
      headers.addCopy(Http::LowerCaseString("x-foo"), "hello  GET /evil HTTP/1.1");
      absl::string_view value =
          headers.get(Http::LowerCaseString("x-foo"))[0]->value().getStringView();
      char* data = const_cast<char*>(value.data());
      data[5] = '\r';
      data[6] = '\n';
    }

    if (!headers.get(Http::LowerCaseString("x-add-mixed-case-header-key")).empty()) {
      // Insert a header key with mixed case. This should be allowed by Envoy.
      headers.addViaMove(Http::HeaderString(absl::string_view("x-MiXeD-CaSe")),
                         Http::HeaderString(absl::string_view("some value here")));
    }

    if (Http::HeaderUtility::isConnect(headers)) {
      headers.removeHost();
    }
    if (!headers.get(Http::LowerCaseString("send-reply")).empty()) {
      decoder_callbacks_->sendLocalReply(Envoy::Http::Code::OK, "", nullptr, absl::nullopt,
                                         "invalid_header_filter_ready");
      return Http::FilterHeadersStatus::StopIteration;
    }
    return Http::FilterHeadersStatus::Continue;
  }
};

constexpr char InvalidHeaderFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<InvalidHeaderFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    decoder_register_;
constexpr char InvalidHeaderFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<InvalidHeaderFilter>,
                                 Server::Configuration::UpstreamHttpFilterConfigFactory>
    decoder_register_upstream_;

} // namespace Envoy
