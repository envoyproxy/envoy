#include "source/server/admin/stats_html_render.h"

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

} // namespace

namespace Envoy {
namespace Server {

StatsHtmlRender::StatsHtmlRender(Http::ResponseHeaderMap& response_headers,
                                 Buffer::Instance& response, const StatsParams& params)
    : StatsTextRender(params) {
  response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Html);
  response.add("<!DOCTYPE html>\n");
  response.add("<html lang='en'>\n");
  response.add(absl::StrReplaceAll(AdminHtmlStart, {{"@FAVICON@", EnvoyFavicon}}));
  response.add("<body>\n");
}

void StatsHtmlRender::finalize(Buffer::Instance& response) {
  ASSERT(!finalized_);
  finalized_ = true;
  if (has_pre_) {
    response.add("</pre>\n");
  }
  response.add("</body>\n");
  response.add("</html>");
}

void StatsHtmlRender::startPre(Buffer::Instance& response) {
  has_pre_ = true;
  response.add("<pre>\n");
}

void StatsHtmlRender::generate(Buffer::Instance& response, const std::string& name,
                               const std::string& value) {
  response.addFragments({name, ": \"", Html::Utility::sanitize(value), "\"\n"});
}

void StatsHtmlRender::noStats(Buffer::Instance& response, absl::string_view types) {
  response.addFragments({"</pre>\n<br/><i>No ", types, " found</i><br/>\n<pre>\n"});
}

void StatsHtmlRender::tableBegin(Buffer::Instance& response) { response.add(AdminHtmlTableBegin); }

void StatsHtmlRender::tableEnd(Buffer::Instance& response) { response.add(AdminHtmlTableEnd); }

void StatsHtmlRender::urlHandler(Buffer::Instance& response, const Admin::UrlHandler& handler,
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
  std::string sanitized_path = Html::Utility::sanitize(path.substr(1));
  path = sanitized_path;

  // Alternate gray and white param-blocks. The pure CSS way of coloring based
  // on row index doesn't work correctly for us as we are using a row for each
  // parameter, and we want each endpoint/option-block to be colored the same.
  const char* row_class = (++index_ & 1) ? " class='gray'" : "";

  // For handlers that mutate state, render the link as a button in a POST form,
  // rather than an anchor tag. This should discourage crawlers that find the /
  // page from accidentally mutating all the server state by GETting all the hrefs.
  const char* method = handler.mutates_server_state_ ? "post" : "get";
  if (submit_on_change_) {
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
    std::string id = absl::StrCat("param-", index_, "-", absl::StrReplaceAll(path, {{"/", "-"}}),
                                  "-", param.id_);
    response.addFragments({"<tr", row_class, ">\n  <td class='option'>"});
    input(response, id, param.id_, path, param.type_, query, param.enum_choices_);
    response.addFragments({"</td>\n  <td class='home-data'>", "<label for='", id, "'>",
                           Html::Utility::sanitize(param.help_), "</label></td>\n</tr>\n"});
  }
}

void StatsHtmlRender::input(Buffer::Instance& response, absl::string_view id,
                            absl::string_view name, absl::string_view path,
                            Admin::ParamDescriptor::Type type,
                            OptRef<const Http::Utility::QueryParams> query,
                            const std::vector<absl::string_view>& enum_choices) {
  std::string value;
  if (query.has_value()) {
    auto iter = query->find(std::string(name));
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
    response.addFragments({"<input type='checkbox' name='", name, "' id='", id, "' form='", path,
                           "'", on_change, value.empty() ? ">" : " checked/>"});
    break;
  case Admin::ParamDescriptor::Type::String: {
    std::string sanitized;
    if (!value.empty()) {
      sanitized = absl::StrCat(" value=", Html::Utility::sanitize(value));
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

} // namespace Server
} // namespace Envoy
