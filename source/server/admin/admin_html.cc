#include "source/common/html/utility.h"
#include "source/server/admin/admin.h"

namespace Envoy {
namespace Server {

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

const char AdminHtmlStart[] = R"(
<head>
  <title>Envoy Admin</title>
  <link rel='shortcut icon' type='image/png' href='@FAVICON@'/>
  <style>
    .home-table {
      font-family: sans-serif;
      font-size: medium;
      border-collapse: collapse;
    }

    .home-row:nth-child(even) {
      background-color: #dddddd;
    }

    .home-data {
      border: 1px solid #dddddd;
      text-align: left;
      padding: 8px;
    }

    .home-form {
      margin-bottom: 0;
    }
  </style>
</head>
<body>
  <table class='home-table'>
    <thead>
      <th class='home-data'>Command</th>
      <th class='home-data'>Description</th>
     </thead>
     <tbody>
)";

const char AdminHtmlEnd[] = R"(
    </tbody>
  </table>
</body>
)";

} // namespace

Http::Code AdminImpl::handlerAdminHome(absl::string_view, Http::ResponseHeaderMap& response_headers,
                                       Buffer::Instance& response, AdminStream&) {
  response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Html);

  response.add(absl::StrReplaceAll(AdminHtmlStart, {{"@FAVICON@", EnvoyFavicon}}));

  // Prefix order is used during searching, but for printing do them in alpha order.
  for (const UrlHandler* handler : sortedHandlers()) {
    absl::string_view path = handler->prefix_;

    if (path == "/") {
      continue; // No need to print self-link to index page.
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

    // For handlers that mutate state, render the link as a button in a POST form,
    // rather than an anchor tag. This should discourage crawlers that find the /
    // page from accidentally mutating all the server state by GETting all the hrefs.
    const char* link_format =
        handler->mutates_server_state_
            ? "<form action='{}' method='post' class='home-form'><button>{}</button></form>"
            : "<a href='{}'>{}</a>";
    const std::string link = fmt::format(link_format, path, path);

    // Handlers are all specified by statically above, and are thus trusted and do
    // not require escaping.
    response.add(fmt::format("<tr class='home-row'><td class='home-data'>{}</td>"
                             "<td class='home-data'>{}</td></tr>\n",
                             link, Html::Utility::sanitize(handler->help_text_)));
  }
  response.add(AdminHtmlEnd);
  return Http::Code::OK;
}

} // namespace Server
} // namespace Envoy
