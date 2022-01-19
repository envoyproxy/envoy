#include "source/server/admin/admin_html_generator.h"

#include <algorithm>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/html/utility.h"
#include "source/server/admin/admin_html_gen.h"

#include "absl/strings/str_replace.h"

namespace {

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

const char AdminHtmlTableBegin[] = R"(
  <table class='home-table'>
    <thead>
      <th class='home-data'>Command</th>
      <th class='home-data'>Description</th>
    </thead>
    <tbody>
)";

const char AdminHtmlTableEnd[] = R"(
    </tbody>
  </table>
)";

} // namespace

namespace Envoy {
namespace Server {

void AdminHtmlGenerator::renderHead() {
  response_.add(absl::StrReplaceAll(AdminHtmlStart, {{"@FAVICON@", EnvoyFavicon}}));
}

void AdminHtmlGenerator::renderTableBegin() { response_.add(AdminHtmlTableBegin); }

void AdminHtmlGenerator::renderTableEnd() { response_.add(AdminHtmlTableEnd); }

void AdminHtmlGenerator::renderUrlHandler(const Admin::UrlHandler& handler,
                                          OptRef<const Http::Utility::QueryParams> query) {
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
  path = path.substr(1);

  // Alternate gray and white param-blocks. The pure CSS way of doing based on
  // row index doesn't work correctly for us we are using a row for each
  // parameter, and we want each endpoint/option-block to be colored the same.
  const char* row_class = (++index_ & 1) ? " class='gray'" : "";

  // For handlers that mutate state, render the link as a button in a POST form,
  // rather than an anchor tag. This should discourage crawlers that find the /
  // page from accidentally mutating all the server state by GETting all the hrefs.
  const char* method = handler.mutates_server_state_ ? "post" : "get";
  if (submit_on_change_) {
    response_.add(absl::StrCat("\n<form action='", path, "' method='", method, "' id='", path,
                               "' class='home-form'></form>\n"));
  } else {
    // Render an explicit visible submit as a link (for GET) or button (for POST).
    const char* button_style = handler.mutates_server_state_ ? "" : " class='button-as-link'";
    response_.add(absl::StrCat("\n<tr class='vert-space'></tr>\n", "<tr", row_class, ">\n",
                               "  <td class='home-data'><form action='", path, "' method='", method,
                               "' id='", path, "' class='home-form'>\n", "    <button",
                               button_style, ">", path, "</button>\n",
                               "  </form></td>\n"
                               "  <td class='home-data'>",
                               Html::Utility::sanitize(handler.help_text_), "</td>\n", "</tr>\n"));
  }

  std::vector<std::string> params;
  for (const Admin::ParamDescriptor& param : handler.params_) {
    response_.add(absl::StrCat("<tr", row_class, ">\n", "  <td class='option'>"));
    renderInput(param.id_, path, param.type_, query, param.enum_choices_);
    response_.add(absl::StrCat("</td>\n", "  <td class='home-data'>",
                               Html::Utility::sanitize(param.help_), "</td>\n", "</tr>\n"));
  }
}

void AdminHtmlGenerator::renderInput(absl::string_view id, absl::string_view path,
                                     Admin::ParamDescriptor::Type type,
                                     OptRef<const Http::Utility::QueryParams> query,
                                     const std::vector<absl::string_view>& enum_choices) {
  std::string value;
  if (query.has_value()) {
    auto iter = query->find(std::string(id));
    if (iter != query->end()) {
      value = iter->second;
    }
  }

  std::string on_change;
  if (submit_on_change_) {
    on_change = absl::StrCat(" onchange='", path, ".submit()'");
  }

  switch (type) {
  case Admin::ParamDescriptor::Type::Boolean:
    response_.add(absl::StrCat("<input type='checkbox' name='", id, "' id='", id, "' form='", path,
                               "'", on_change, value.empty() ? "" : " checked", "/>"));
    break;
  case Admin::ParamDescriptor::Type::String:
    response_.add(absl::StrCat("<input type='text' name='", id, "' id='", id, "' form='", path, "'",
                               on_change, value.empty() ? "" : absl::StrCat(" value='", value, "'"),
                               "/>"));
    break;
  case Admin::ParamDescriptor::Type::Hidden:
    response_.add(absl::StrCat("<input type='hidden' name='", id, "' id='", id, "' form='", path,
                               "'", on_change,
                               value.empty() ? "" : absl::StrCat(" value='", value, "'"), "/>"));
    break;
  case Admin::ParamDescriptor::Type::Enum:
    response_.add(absl::StrCat("\n    <select name='", id, "' id='", id, "' form='", path, "'",
                               on_change, ">\n"));
    for (absl::string_view choice : enum_choices) {
      std::string sanitized = Html::Utility::sanitize(choice);
      response_.add(absl::StrCat("      <option value='", sanitized, "'",
                                 (value == sanitized) ? " selected" : "", ">", sanitized,
                                 "</option>\n"));
    }
    response_.add("    </select>\n  ");
    break;
  }
}

} // namespace Server
} // namespace Envoy
