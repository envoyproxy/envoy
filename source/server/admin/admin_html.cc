#include "source/common/html/utility.h"
#include "source/server/admin/admin.h"
#include "source/server/admin/html/admin_html_gen.h"
#include "source/server/admin/stats_html_render.h"

namespace Envoy {
namespace Server {

namespace AdminHtml {

class BuiltinHtmlResourceProvider : public HtmlResourceProvider {
 public:
  BuiltinHtmlResourceProvider() {
    map_["admin_head_start.html"] = AdminHtmlStart;
    map_["admin.css"] = AdminCss;
    map_["active_stats.js"] = AdminActiveStatsJs;
    map_["active_params.html"] = AdminActiveParamsHtml;
  }

  absl::string_view getResource(absl::string_view resource_name, std::string&) override {
    return map_[resource_name];
  }

 private:
  absl::flat_hash_map<absl::string_view, absl::string_view> map_;
};

struct ProviderContainer {
  std::unique_ptr<HtmlResourceProvider> provider_{std::make_unique<BuiltinHtmlResourceProvider>()};
};

ProviderContainer& getProviderContainer() {
  MUTABLE_CONSTRUCT_ON_FIRST_USE(ProviderContainer);
}

absl::string_view getResource(absl::string_view resource_name, std::string& buf) {
  return getProviderContainer().provider_->getResource(resource_name, buf);
}

void setHtmlResourceProvider(std::unique_ptr<HtmlResourceProvider> resource_provider) {
  getProviderContainer().provider_ = std::move(resource_provider);
}

} // namespace AdmiHtml

Http::Code AdminImpl::handlerAdminHome(Http::ResponseHeaderMap& response_headers,
                                       Buffer::Instance& response, AdminStream&) {
  StatsHtmlRender html(response_headers, response, StatsParams());
  html.tableBegin(response);

  // Prefix order is used during searching, but for printing do them in alpha order.
  OptRef<const Http::Utility::QueryParams> no_query_params;
  for (const UrlHandler* handler : sortedHandlers()) {
    html.urlHandler(response, *handler, no_query_params);
  }

  html.tableEnd(response);
  html.finalize(response);

  return Http::Code::OK;
}

} // namespace Server
} // namespace Envoy
