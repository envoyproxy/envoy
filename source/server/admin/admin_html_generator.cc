#include "source/server/admin/admin_html_generator.h"

#include <algorithm>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/html/utility.h"

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

const char AdminHtmlStart[] = R"(
<head>
  <title>Envoy Admin</title>
  <link rel='shortcut icon' type='image/png' href='@FAVICON@'/>
  <!--
     <link rel='stylesheet' href='http://localhost:8081/admin.css' />
     <script src="http://localhost:8081/admin.js"></script>
  -->
  <style>
    .home-table {
      font-family: sans-serif;
      font-size: medium;
      border-collapse: collapse;
      border-spacing: 0;
    }

    .home-data {
      text-align: left;
      padding: 4px;
    }

    .home-form {
      margin-bottom: 0;
    }

    .button-as-link {
      background: none!important;
      border: none;
      padding: 0!important;
      font-family: sans-serif;
      font-size: medium;
      color: #069;
      text-decoration: underline;
      cursor: pointer;
    }

    .gray {
      background-color: #dddddd;
    }

    .vert-space {
      height: 4px;
    }

    .option {
      padding-bottom: 4px;
      padding-top: 4px;
      padding-right: 4px;
      padding-left: 20px;
      text-align: right;
    }

    #scopes-outline {
      margin: 0;
      padding: 0;
      font-family: sans-serif
    }

    .scope-children {
      list-style-type: none;
    }

    /* Style the caret/arrow */
    .caret {
      cursor: pointer;
      user-select: none; /* Prevent text selection */
    }

    /* Create the caret/arrow with a unicode, and style it */
    .caret::before {
      content: "\25B6";
      color: black;
      display: inline-block;
      margin-right: 6px;
    }

    /* Rotate the caret/arrow icon when clicked on (using JavaScript) */
    .caret-down::before {
      transform: rotate(90deg);
    }*/
  </style>
  <script>
    function setScope(scope) {
      document.getElementById("scope").value = scope;
      document.getElementById("stats").submit();
    }

    function expandScope(parent, scope) {
      let url = 'stats?scope=' + scope + '&format=json';
      url += '&type=' + document.getElementById('type').value;
      if (document.getElementById('usedonly').checked) {
        url += '&usedonly';
      }
      fetch(url).then(response => response.json())
          .then(json => populateScope(parent, scope, json));
    }

    function addScope(parent, scope) {
      const li_tag = document.createElement('li');
      const span_tag = document.createElement('span');
      span_tag.setAttribute('class', 'caret');
      span_tag.textContent = scope;
      const children_tag = document.createElement('ul');
      children_tag.setAttribute('class', 'scope-children');
      li_tag.appendChild(span_tag);
      li_tag.appendChild(children_tag);
      parent.appendChild(li_tag);

      span_tag.addEventListener("click", () => {
        span_tag.classList.toggle("caret-down");
        if (children_tag.firstChild) {
          while (children_tag.firstChild) {
            children_tag.removeChild(children_tag.firstChild);
          }
        } else {
          expandScope(children_tag, scope);
        }
      });
    }

    function populateScopes(json) {
      const parent = document.getElementById('scopes-outline');
      for (let scope of json.scopes) {
        addScope(parent, scope);
    /*
        const a_tag = document.createElement('a');
        a_tag.id = 'scope_' + scope;
        a_tag.setAttribute("href", 'javascript:expandScope("' + scope + '")');
        a_tag.textContent = scope;
        document.body.appendChild(a_tag);
        document.body.appendChild(document.createElement('br'));
    */
      }
    }

    function populateScope(parent, scope, json) {
      //let prev = document.getElementById('scope_' + scope);

      for (let subscope of json.scopes) {
    /*
    const br = document.createElement('br');
    prev.parentNode.insertBefore(br, prev.nextSibling);
    prev = br;
    */

    addScope(parent, subscope);
    /*
        const a_tag = document.createElement('a');
        a_tag.textContent = subscope;
        a_tag.id = 'scope_' + subscope;
        a_tag.setAttribute("href", 'javascript:expandScope("' + subscope + '")');
        prev.parentNode.insertBefore(a_tag, prev.nextSibling);
        prev = a_tag;
    */
      }

      for (let stat of json.stats) {
        if (stat.histograms && stat.histograms.computed_quantiles) {
          for (let histogram of stat.histograms.computed_quantiles) {
            let val_strs = [];
            for (let value of histogram.values) {
              if (value.interval || value.cumulative) {
                val_strs.push('(' + value.interval + ',' + value.cumulative + ')')
              }
            }
            const val_str = (val_strs.length == 0) ? 'no values' : ('[' + val_strs.join(',') + ']');
            const li_tag = document.createElement('li');
            li_tag.textContent = histogram.name + ": " + val_str;
            parent.appendChild(li_tag);
          }
        } else {
          const li_tag = document.createElement('li');
          li_tag.textContent = stat.name + ": " + stat.value;
          parent.appendChild(li_tag);
        }
      }
    /*
      const pre = document.createElement('pre');
      pre.textContent = lines.join('\n');
      prev.parentNode.insertBefore(pre, prev.nextSibling);
    */
    }
  </script>
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

namespace Envoy {
namespace Server {

void AdminHtmlGenerator::renderHead() {
  response_.add(absl::StrReplaceAll(AdminHtmlStart, {{"@FAVICON@", EnvoyFavicon}}));
}

void AdminHtmlGenerator::renderTail() { response_.add(AdminHtmlEnd); }

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
  const char* button_style = handler.mutates_server_state_ ? "" : " class='button-as-link'";
  const char* method = handler.mutates_server_state_ ? "post" : "get";
  if (visible_submit_) {
    response_.add(absl::StrCat("\n<tr class='vert-space'></tr>\n", "<tr", row_class, ">\n",
                               "  <td class='home-data'><form action='", path, "' method='", method,
                               "' id='", path, "' class='home-form'>\n", "    <button",
                               button_style, ">", path, "</button>\n",
                               "  </form></td>\n"
                               "  <td class='home-data'>",
                               Html::Utility::sanitize(handler.help_text_), "</td>\n", "</tr>\n"));
  } else {
    response_.add(absl::StrCat("\n<form action='", path, "' method='", method, "' id='", path,
                               "' class='home-form'></form>\n"));
  }

  std::vector<std::string> params;
  for (const Admin::ParamDescriptor& param : handler.params_) {
    response_.add(absl::StrCat("<tr", row_class, ">\n", "  <td class='option'>"));
    renderInput(param.id_, path, param.type_, query, param.choices_);
    response_.add(absl::StrCat("</td>\n", "  <td class='home-data'>",
                               Html::Utility::sanitize(param.help_), "</td>\n", "</tr>\n"));
  }
}

void AdminHtmlGenerator::renderInput(absl::string_view id, absl::string_view path,
                                     Admin::ParamDescriptor::Type type,
                                     OptRef<const Http::Utility::QueryParams> query,
                                     const std::vector<absl::string_view>& choices) {
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
  case Admin::ParamDescriptor::Type::Mask: {
    response_.add("<table class='home-table'><tbody>\n");
    for (size_t row = 0; row < (choices.size() + 1) / 2; ++row) {
      response_.add("  <tr>\n    ");
      uint32_t last_index = std::min(2 * (row + 1), choices.size());
      for (size_t i = 2 * row; i < last_index; ++i) {
        response_.add(absl::StrCat("    <td><input type='checkbox' name='", choices[i], "' id='",
                                   choices[i], "' checked>", choices[i], "</input></td>\n"));
      }
      response_.add("  </tr>\n");
    }
    response_.add("</tbody></table>\n  ");
    break;
  }
  case Admin::ParamDescriptor::Type::Enum:
    response_.add(absl::StrCat("\n    <select name='", id, "' id='", id, "' form='", path, "'",
                               on_change, ">\n"));
    for (absl::string_view choice : choices) {
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
