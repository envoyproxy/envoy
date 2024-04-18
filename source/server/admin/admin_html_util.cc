#include "source/server/admin/admin_html_util.h"

#include "source/common/html/utility.h"
#include "source/common/http/headers.h"
#include "source/server/admin/html/admin_html_gen.h"

#include "absl/strings/str_replace.h"

namespace {

const char AdminHtmlTableBegin[] = R"(
  <table class='home-table'>
    <thead>
      <tr>
        <th class='home-data'>Command</th>
        <th class='home-data'>Description</th>
      </tr>
    </thead>
    <tbody>
)";

const char AdminHtmlTableEnd[] = R"(
    </tbody>
  </table>
)";

/**
 * Favicon base64 image was harvested by screen-capturing the favicon from a Chrome tab
 * while visiting https://www.envoyproxy.io/. The resulting PNG was translated to base64
 * by dropping it into https://www.base64-image.de/ and then pasting the resulting string
 * below.
 *
 * The actual favicon source for that, https://www.envoyproxy.io/img/favicon.ico is nicer
 * because it's transparent, but is also 67646 bytes, which is annoying to inline. We could
 * just reference that rather than inlining it, but then the favicon won't work when visiting
 * the admin page from a network that can't see the internet.
 */
const char EnvoyFavicon[] =
    "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABgAAAAYCAYAAADgdz34AAAAAXNSR0IArs4c6QAAAARnQU1"
    "BAACxjwv8YQUAAAAJcEhZcwAAEnQAABJ0Ad5mH3gAAAH9SURBVEhL7ZRdTttAFIUrUFaAX5w9gIhgUfzshFRK+gIbaVbA"
    "zwaqCly1dSpKk5A485/YCdXpHTB4BsdgVe0bD0cZ3Xsm38yZ8byTUuJ/6g3wqqoBrBhPTzmmLfptMbAzttJTpTKAF2MWC"
    "7ADCdNIwXZpvMMwayiIwwS874CcOc9VuQPR1dBBChPMITpFXXU45hukIIH6kHhzVqkEYB8F5HYGvZ5B7EvwmHt9K/59Cr"
    "U3QbY2RNYaQPYmJc+jPIBICNCcg20ZsAsCPfbcrFlRF+cJZpvXSJt9yMTxO/IAzJrCOfhJXiOgFEX/SbZmezTWxyNk4Q9"
    "anHMmjnzAhEyhAW8LCE6wl26J7ZFHH1FMYQxh567weQBOO1AW8D7P/UXAQySq/QvL8Fu9HfCEw4SKALm5BkC3bwjwhSKr"
    "A5hYAMXTJnPNiMyRBVzVjcgCyHiSm+8P+WGlnmwtP2RzbCMiQJ0d2KtmmmPorRHEhfMROVfTG5/fYrF5iWXzE80tfy9WP"
    "sCqx5Buj7FYH0LvDyHiqd+3otpsr4/fa5+xbEVQPfrYnntylQG5VGeMLBhgEfyE7o6e6qYzwHIjwl0QwXSvvTmrVAY4D5"
    "ddvT64wV0jRrr7FekO/XEjwuwwhuw7Ef7NY+dlfXpLb06EtHUJdVbsxvNUqBrwj/QGeEUSfwBAkmWHn5Bb/gAAAABJRU5";

} // namespace

namespace Envoy {
namespace Server {

namespace {
class BuiltinResourceProvider : public AdminHtmlUtil::ResourceProvider {
public:
  BuiltinResourceProvider() : histogram_js_(absl::StrCat(HistogramsJs1, HistogramsJs2)) {
    map_["admin_head_start.html"] = AdminHtmlStart;
    map_["admin.css"] = AdminCss;
    map_["active_stats.js"] = AdminActiveStatsJs;
    map_["histograms.js"] = histogram_js_;
    map_["active_params.html"] = AdminActiveParamsHtml;
  }

  absl::string_view getResource(absl::string_view resource_name, std::string&) override {
    return map_[resource_name];
  }

private:
  const std::string histogram_js_;
  absl::flat_hash_map<absl::string_view, absl::string_view> map_;
};

// This is a hack to make a lazy-constructed holder for a pointer to the
// resource provider.
//
// We use a mutable singleton rather than plumbing in the resource
// provider. This is because the HTML features in the admin console can be
// compiled out of Envoy. It's awkward to plumb the resource provider through
// multiple layers that do not want to compile in the class definition. There
// would be many `ifdefs` through constructors, initializers, etc.
struct ProviderContainer {
  std::unique_ptr<AdminHtmlUtil::ResourceProvider> provider_{
      std::make_unique<BuiltinResourceProvider>()};
};

ProviderContainer& getProviderContainer() { MUTABLE_CONSTRUCT_ON_FIRST_USE(ProviderContainer); }

} // namespace

absl::string_view AdminHtmlUtil::getResource(absl::string_view resource_name, std::string& buf) {
  return getProviderContainer().provider_->getResource(resource_name, buf);
}

std::unique_ptr<AdminHtmlUtil::ResourceProvider>
AdminHtmlUtil::setResourceProvider(std::unique_ptr<ResourceProvider> resource_provider) {
  ProviderContainer& container = getProviderContainer();
  std::unique_ptr<ResourceProvider> prev = std::move(container.provider_);
  container.provider_ = std::move(resource_provider);
  return prev;
}

void AdminHtmlUtil::renderHead(Http::ResponseHeaderMap& response_headers,
                               Buffer::Instance& response) {
  response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Html);
  std::string buf1, buf2, buf3;
  response.addFragments({"<!DOCTYPE html>\n"
                         "<html lang='en'>\n"
                         "<head>\n",
                         absl::StrReplaceAll(getResource("admin_head_start.html", buf1),
                                             {{"@FAVICON@", EnvoyFavicon}}),
                         "<style>\n", getResource("admin.css", buf2), "</style>\n"});
  response.add("</head>\n<body>\n");
}

void AdminHtmlUtil::renderTableBegin(Buffer::Instance& response) {
  response.add(AdminHtmlTableBegin);
}

void AdminHtmlUtil::renderTableEnd(Buffer::Instance& response) { response.add(AdminHtmlTableEnd); }

void AdminHtmlUtil::finalize(Buffer::Instance& response) { response.add("</body>\n</html>\n"); }

void AdminHtmlUtil::renderHandlerParam(Buffer::Instance& response, absl::string_view id,
                                       absl::string_view name, absl::string_view path,
                                       Admin::ParamDescriptor::Type type,
                                       OptRef<const Http::Utility::QueryParamsMulti> query,
                                       const std::vector<absl::string_view>& enum_choices,
                                       bool submit_on_change) {
  std::string value;
  if (query.has_value()) {
    auto result = query->getFirstValue(name);
    if (result.has_value()) {
      value = result.value();
    }
  }

  std::string on_change;
  if (submit_on_change) {
    on_change = absl::StrCat(" onchange='", path, ".submit()'");
  }

  switch (type) {
  case Admin::ParamDescriptor::Type::Boolean:
    response.addFragments({"<input type='checkbox' name='", name, "' id='", id, "' form='", path,
                           "'", on_change, value.empty() ? ">" : " checked/>"});
    break;
  case Admin::ParamDescriptor::Type::String: {
    std::string sanitized;
    if (!value.empty()) {
      sanitized = absl::StrCat(" value='", Html::Utility::sanitize(value), "'");
    }
    response.addFragments({"<input type='text' name='", name, "' id='", id, "' form='", path, "'",
                           on_change, sanitized, " />"});
    break;
  }
  case Admin::ParamDescriptor::Type::Enum:
    response.addFragments(
        {"\n    <select name='", name, "' id='", id, "' form='", path, "'", on_change, ">\n"});
    for (absl::string_view choice : enum_choices) {
      std::string sanitized_choice = Html::Utility::sanitize(choice);
      std::string sanitized_value = Html::Utility::sanitize(value);
      absl::string_view selected = (sanitized_value == sanitized_choice) ? " selected" : "";
      response.addFragments({"      <option value='", sanitized_choice, "'", selected, ">",
                             sanitized_choice, "</option>\n"});
    }
    response.add("    </select>\n  ");
    break;
  }
}

void AdminHtmlUtil::renderEndpointTableRow(Buffer::Instance& response,
                                           const Admin::UrlHandler& handler,
                                           OptRef<const Http::Utility::QueryParamsMulti> query,
                                           int index, bool submit_on_change, bool active) {
  absl::string_view path = handler.prefix_;

  if (path == "/") {
    return; // No need to print self-link to index page.
  }

  // Remove the leading slash from the link, so that the admin page can be
  // rendered as part of another console, on a sub-path.
  //
  // E.g. consider a downstream dashboard that embeds the Envoy admin console.
  // In that case, the "/stats" endpoint would be at
  // https://DASHBOARD/envoy_admin/stats. If the links we present on the home
  // page are absolute (e.g. "/stats") they won't work in the context of the
  // dashboard. Removing the leading slash, they will work properly in both
  // the raw admin console and when embedded in another page and URL
  // hierarchy.
  ASSERT(!path.empty());
  ASSERT(path[0] == '/');
  std::string sanitized_path = Html::Utility::sanitize(path.substr(1));
  path = sanitized_path;

  // Alternate gray and white param-blocks. The pure CSS way of coloring based
  // on row index doesn't work correctly for us as we are using a row for each
  // parameter, and we want each endpoint/option-block to be colored the same.
  const char* row_class = (index & 1) ? " class='gray'" : "";

  // For handlers that mutate state, render the link as a button in a POST form,
  // rather than an anchor tag. This should discourage crawlers that find the /
  // page from accidentally mutating all the server state by GETting all the hrefs.
  const char* method = handler.mutates_server_state_ ? "post" : "get";
  if (submit_on_change) {
    response.addFragments({"\n<tr><td><form action='", path, "' method='", method, "' id='", path,
                           "' class='home-form'></form></td><td></td></tr>\n"});
  } else {
    response.addFragments({"\n<tr class='vert-space'><td></td><td></td></tr>\n<tr", row_class,
                           ">\n  <td class='home-data'>"});
    if (!handler.mutates_server_state_ && handler.params_.empty()) {
      // GET requests without parameters can be simple links rather than forms with
      // buttons that are rendered as links. This simplification improves the
      // usability of the page with screen-readers.
      response.addFragments({"<a href='", path, "'>", path, "</a>"});
    } else {
      // Render an explicit visible submit as a link (for GET) or button (for POST).
      const char* button_style = handler.mutates_server_state_ ? "" : " class='button-as-link'";
      response.addFragments({"<form action='", path, "' method='", method, "' id='", path,
                             "' class='home-form'>\n    <button", button_style, ">", path,
                             "</button>\n  </form>"});
    }
    response.addFragments({"</td>\n  <td class='home-data'>",
                           Html::Utility::sanitize(handler.help_text_), "</td>\n</tr>\n"});
  }

  for (const Admin::ParamDescriptor& param : handler.params_) {
    // Give each parameter a unique number. Note that this naming is also referenced in
    // active_stats.js which looks directly at the parameter widgets to find the
    // current values during JavaScript-driven active updates.
    std::string id =
        absl::StrCat("param-", index, "-", absl::StrReplaceAll(path, {{"/", "-"}}), "-", param.id_);
    response.addFragments({"<tr", row_class, ">\n  <td class='option'>"});
    renderHandlerParam(response, id, param.id_, path, param.type_, query, param.enum_choices_,
                       submit_on_change && (!active || param.id_ == "format"));
    response.addFragments({"</td>\n  <td class='home-data'>", "<label for='", id, "'>",
                           Html::Utility::sanitize(param.help_), "</label></td>\n</tr>\n"});
  }
}

} // namespace Server
} // namespace Envoy
