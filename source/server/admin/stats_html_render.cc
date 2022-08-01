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

  /*
    html_.setVisibleSubmit(false);
    html_.setSubmitOnChange(true);
    html_.renderHead();
    html_.renderTableBegin();
    html_.renderUrlHandler(stats_handler.statsHandler(), params.query_);
    html_.renderInput("scope", "stats", Admin::ParamDescriptor::Type::Hidden, params.query_, {});
    html_.renderTableEnd();
  */
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
  case Admin::ParamDescriptor::Type::Hidden:
    response_.add(absl::StrCat("<input type='hidden' name='", id, "' id='", id, "' form='", path,
                               "'", on_change,
                               value.empty() ? "" : absl::StrCat(" value='", value, "'"), "/>"));
    break;
    /*
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
    */
  }
}

void render(Buffer::Instance&) override {
  for (const auto& iter : groups_) {
    absl::string_view label = StatsHandler::typeToString(iter.first);
    const Group& group = iter.second;
    if (group.empty()) {
      response_.add(absl::StrCat("<br/><i>No ", label, " found</i><br/>\n"));
    } else {
      response_.add(absl::StrCat("<h1>", label, "</h1>\n<pre>\n"));
      for (const std::string& str : group) {
        response_.add(str);
      }
      response_.add("</pre>\n");
    }
  }
}

static void renderAjaxRequestForScopes(Buffer::Instance& response, const Params& params) {
  // Delegate to JavaScript to fetch all top-level scopes and render as an outline.
  std::string url =
      absl::StrCat("stats?format=json&show_json_scopes&type=", typeToString(params.type_));
  if (params.used_only_) {
    url += "&usedonly";
  }
  if (!params.filter_string_.empty()) {
    absl::StrAppend(&url, "&filter=", params.filter_string_);
  }
  response.add(absl::StrCat("<ul id='scopes-outline'></ul>\n"
                            "<script>\n"
                            "  fetch('",
                            url, "')\n", "    .then(response => response.json())\n",
                            "    .then(data => populateScopes(data));\n"
                            "</script>\n"));
}

class StatsHandler::Context {
public:
  // We need to hold ref-counts to each stat in our intermediate sets to avoid
  // having the stats be deleted while we are computing results.
  template <class StatType> struct Hash {
    size_t operator()(const Stats::RefcountPtr<StatType>& a) const { return a->statName().hash(); }
  };

  template <class StatType> struct Compare {
    bool operator()(const Stats::RefcountPtr<StatType>& a,
                    const Stats::RefcountPtr<StatType>& b) const {
      return a->statName() == b->statName();
    }
  };

  template <class StatType>
  using SharedStatSet =
      absl::flat_hash_set<Stats::RefcountPtr<StatType>, Hash<StatType>, Compare<StatType>>;

  Context(const Params& params, Render& render, Buffer::Instance& response)
      : params_(params), render_(render), response_(response) {}

  void collectScope(const Stats::Scope& scope) {
    collect<Stats::TextReadout>(Type::TextReadouts, scope, text_readouts_);
    collect<Stats::Counter>(Type::Counters, scope, counters_);
    collect<Stats::Gauge>(Type::Gauges, scope, gauges_);
    collect<Stats::Histogram>(Type::Histograms, scope, histograms_);
  }

  /** @return true if any of the stats in the scope pass the filter regex and used-only.*/
  bool hasAny(const Stats::Scope& scope) {
    return has<Stats::TextReadout>(Type::TextReadouts, scope) ||
           has<Stats::Counter>(Type::Counters, scope) || has<Stats::Gauge>(Type::Gauges, scope) ||
           has<Stats::Histogram>(Type::Histograms, scope);
  }

  void emit() {
    emit<Stats::TextReadout>(Type::TextReadouts, text_readouts_);
    emit<Stats::Counter>(Type::Counters, counters_);
    emit<Stats::Gauge>(Type::Gauges, gauges_);
    emit<Stats::Histogram>(Type::Histograms, histograms_);
    emitScopes();
  }

  template <class StatType>
  void collect(Type type, const Stats::Scope& scope, SharedStatSet<StatType>& set) {
    // Bail early if the  requested type does not match the current type.
    if (params_.type_ != Type::All && params_.type_ != type) {
      return;
    }

    Stats::IterateFn<StatType> fn = [this, &set](const Stats::RefcountPtr<StatType>& stat) -> bool {
      if (params_.shouldShowMetric(*stat)) {
        set.insert(stat);
      }
      return true;
    };
    scope.iterate(fn);
  }

  /** @return true if any stats matching the filter and used-only are in the scope. */
  template <class StatType> bool has(Type type, const Stats::Scope& scope) {
    // Bail early if the  requested type does not match the current type.
    if (params_.type_ != Type::All && params_.type_ != type) {
      return false;
    }

    Stats::IterateFn<StatType> fn = [this](const Stats::RefcountPtr<StatType>& stat) -> bool {
      // Stop iteration if any stat matches the filter by returning false.
      return !params_.shouldShowMetric(*stat);
    };
    return !scope.iterate(fn); // We found a match if the iteration was stopped.
  }

  template <class StatType> void emit(Type type, SharedStatSet<StatType>& set) {
    // Bail early if the  requested type does not match the current type.
    if (params_.type_ != Type::All && params_.type_ != type) {
      return;
    }

    if (set.empty()) {
      render_.noStats(type);
    }

    std::vector<Stats::RefcountPtr<StatType>> sorted;
    sorted.reserve(set.size());
    for (const Stats::RefcountPtr<StatType>& stat : set) {
      sorted.emplace_back(stat);
    }

    struct Cmp {
      bool operator()(const Stats::RefcountPtr<StatType>& a,
                      const Stats::RefcountPtr<StatType>& b) const {
        return a->constSymbolTable().lessThan(a->statName(), b->statName());
      }
    };
    std::sort(sorted.begin(), sorted.end(), Cmp());

    for (const Stats::RefcountPtr<StatType>& stat : sorted) {
      render_.generate(*stat);
    }
  }

  void emitScopes() {
    // Prune the scopes-list so that if both "a.b.c" and "a.b" are present, we drop "a.b.c".
    for (const std::string& scope : scopes_) {
      size_t last_dot = scope.rfind('.');
      if (last_dot != 0 && last_dot != std::string::npos &&
          scopes_.find(scope.substr(0, last_dot)) != scopes_.end()) {
        continue;
      }
      render_.addScope(scope);
    }
  }

  void addScope(const Stats::Scope& scope, const std::string& scope_name) {
    if (scope_name != params_.scope_ && (params_.matchesAny() || hasAny(scope))) {
      scopes_.insert(scope_name);
    }
  }

  Render& render() { return render_; }

  const Params& params_;
  Render& render_;
  Buffer::Instance& response_;

  SharedStatSet<Stats::Counter> counters_;
  SharedStatSet<Stats::Gauge> gauges_;
  SharedStatSet<Stats::TextReadout> text_readouts_;
  SharedStatSet<Stats::Histogram> histograms_;
  std::set<std::string> scopes_;
};

Http::Code StatsHandler::stats(const Params& params, Stats::Store& stats,
                               Http::ResponseHeaderMap& response_headers,
                               Buffer::Instance& response) {
  std::unique_ptr<Render> render;

  switch (params.format_) {
  case Format::Html:
    render = std::make_unique<HtmlRender>(response_headers, response, *this, params);
    break;
  case Format::Json:
    render = std::make_unique<JsonRender>(params);
    response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
    break;
  case Format::Prometheus:
    ASSERT(false);
    ABSL_FALLTHROUGH_INTENDED;
  case Format::Text:
    render = std::make_unique<TextRender>();
    break;
  }

  // The default HTML view, with no scope query-param specified (or an empty string), we will
  // just generate some HTML to initiate a JSON request to populate the scopes. This way the
  // rendering for scopes and sub-scopes can be handled consistently in JavaScript.
  if (params.scope_.empty() && params.format_ == Format::Html) {
    StatsHandler::HtmlRender::renderAjaxRequestForScopes(response, params);
  } else {
    Context context(params, *render, response);

    // Note that multiple scopes can exist with the same name, and also that
    // scopes may be created/destroyed in other threads, whenever we are not
    // holding the store's lock. So when we traverse the scopes, we'll both
    // look for Scope objects matching our expected name, and we'll create
    // a sorted list of sort names for use in populating next/previous buttons.
    auto scope_fn = [this, &params, &context](const Stats::Scope& scope) {
      std::string prefix_str = server_.stats().symbolTable().toString(scope.prefix());

      if (params.query_.find("show_json_scopes") != params.query_.end()) {
        if (params.scope_ == prefix_str) {
          context.collectScope(scope);
        } else if (params.scope_.empty() ||
                   absl::StartsWith(prefix_str + ".", params.scope_ + ".")) {
          // Truncate any hierarchy after the prefix.
          size_t dot_search = params.scope_.empty() ? 0 : params.scope_.size() + 1;
          size_t dot = prefix_str.find('.', dot_search);
          if (dot != std::string::npos) {
            prefix_str.resize(dot);
          }
          context.addScope(scope, prefix_str);
        }
      } else if (prefix_str == params.scope_ || prefix_str.empty() || params.scope_.empty()) {
        // If the scope matches the prefix of what the user wants, append in the
        // stats from it. Note that scopes with a prefix of "" will match anything
        // the user types, in which case we'll still be filtering based on stat
        // name prefix.
        context.collectScope(scope);
      } else if (absl::StartsWith(prefix_str, params.scope_) &&
                 prefix_str[params.scope_.size()] == '.') {
        context.addScope(scope, prefix_str);
      }
    };
    stats.forEachScope([](size_t) {}, scope_fn);
    context.emit();
    render->render(response);
  }

  if (params.format_ == Format::Html) {
    response.add("</body>\n");
  }

  // Display plain stats if format query param is not there.
  // statsAsText(counters_and_gauges, text_readouts, histograms, response);
  return Http::Code::OK;
}

class StatsHandler::HtmlRender : public StatsHandler::TextRender {
 public:
  void render(Buffer::Instance&) override {
    for (const auto& iter : groups_) {
      absl::string_view label = StatsHandler::typeToString(iter.first);
      const Group& group = iter.second;
      if (group.empty()) {
        response_.add(absl::StrCat("<br/><i>No ", label, " found</i><br/>\n"));
      } else {
        response_.add(absl::StrCat("<h1>", label, "</h1>\n<pre>\n"));
        for (const std::string& str : group) {
          response_.add(str);
        }
        response_.add("</pre>\n");
      }
    }
  }

  static void renderAjaxRequestForScopes(Buffer::Instance& response, const Params& params) {
    // Delegate to JavaScript to fetch all top-level scopes and render as an outline.
    std::string url =
        absl::StrCat("stats?format=json&show_json_scopes&type=", typeToString(params.type_));
    if (params.used_only_) {
      url += "&usedonly";
    }
    if (!params.filter_string_.empty()) {
      absl::StrAppend(&url, "&filter=", params.filter_string_);
    }
    response.add(absl::StrCat("<ul id='scopes-outline'></ul>\n"
                              "<script>\n"
                              "  fetch('",
                              url, "')\n", "    .then(response => response.json())\n",
                              "    .then(data => populateScopes(data));\n"
                              "</script>\n"));
  }

 private:
  Buffer::Instance& response_;
  AdminHtmlGenerator html_;
};

class StatsHandler::Context {
 public:
  // We need to hold ref-counts to each stat in our intermediate sets to avoid
  // having the stats be deleted while we are computing results.
  template <class StatType> struct Hash {
    size_t operator()(const Stats::RefcountPtr<StatType>& a) const { return a->statName().hash(); }
  };

  template <class StatType> struct Compare {
    bool operator()(const Stats::RefcountPtr<StatType>& a,
                    const Stats::RefcountPtr<StatType>& b) const {
      return a->statName() == b->statName();
    }
  };

  template <class StatType>
      using SharedStatSet =
      absl::flat_hash_set<Stats::RefcountPtr<StatType>, Hash<StatType>, Compare<StatType>>;

  Context(const Params& params, Render& render, Buffer::Instance& response)
      : params_(params), render_(render), response_(response) {}

  void collectScope(const Stats::Scope& scope) {
    collect<Stats::TextReadout>(Type::TextReadouts, scope, text_readouts_);
    collect<Stats::Counter>(Type::Counters, scope, counters_);
    collect<Stats::Gauge>(Type::Gauges, scope, gauges_);
    collect<Stats::Histogram>(Type::Histograms, scope, histograms_);
  }

  /** @return true if any of the stats in the scope pass the filter regex and used-only.*/
  bool hasAny(const Stats::Scope& scope) {
    return has<Stats::TextReadout>(Type::TextReadouts, scope) ||
        has<Stats::Counter>(Type::Counters, scope) || has<Stats::Gauge>(Type::Gauges, scope) ||
        has<Stats::Histogram>(Type::Histograms, scope);
  }

  void emit() {
    emit<Stats::TextReadout>(Type::TextReadouts, text_readouts_);
    emit<Stats::Counter>(Type::Counters, counters_);
    emit<Stats::Gauge>(Type::Gauges, gauges_);
    emit<Stats::Histogram>(Type::Histograms, histograms_);
    emitScopes();
  }

  template <class StatType>
      void collect(Type type, const Stats::Scope& scope, SharedStatSet<StatType>& set) {
    // Bail early if the  requested type does not match the current type.
    if (params_.type_ != Type::All && params_.type_ != type) {
      return;
    }

    Stats::IterateFn<StatType> fn = [this, &set](const Stats::RefcountPtr<StatType>& stat) -> bool {
      if (params_.shouldShowMetric(*stat)) {
        set.insert(stat);
      }
      return true;
    };
    scope.iterate(fn);
  }

  /** @return true if any stats matching the filter and used-only are in the scope. */
  template <class StatType> bool has(Type type, const Stats::Scope& scope) {
    // Bail early if the  requested type does not match the current type.
    if (params_.type_ != Type::All && params_.type_ != type) {
      return false;
    }

    Stats::IterateFn<StatType> fn = [this](const Stats::RefcountPtr<StatType>& stat) -> bool {
      // Stop iteration if any stat matches the filter by returning false.
      return !params_.shouldShowMetric(*stat);
    };
    return !scope.iterate(fn); // We found a match if the iteration was stopped.
  }

  template <class StatType> void emit(Type type, SharedStatSet<StatType>& set) {
    // Bail early if the  requested type does not match the current type.
    if (params_.type_ != Type::All && params_.type_ != type) {
      return;
    }

    if (set.empty()) {
      render_.noStats(type);
    }

    std::vector<Stats::RefcountPtr<StatType>> sorted;
    sorted.reserve(set.size());
    for (const Stats::RefcountPtr<StatType>& stat : set) {
      sorted.emplace_back(stat);
    }

    struct Cmp {
      bool operator()(const Stats::RefcountPtr<StatType>& a,
                      const Stats::RefcountPtr<StatType>& b) const {
        return a->constSymbolTable().lessThan(a->statName(), b->statName());
      }
    };
    std::sort(sorted.begin(), sorted.end(), Cmp());

    for (const Stats::RefcountPtr<StatType>& stat : sorted) {
      render_.generate(*stat);
    }
  }

  void emitScopes() {
    // Prune the scopes-list so that if both "a.b.c" and "a.b" are present, we drop "a.b.c".
    for (const std::string& scope : scopes_) {
      size_t last_dot = scope.rfind('.');
      if (last_dot != 0 && last_dot != std::string::npos &&
          scopes_.find(scope.substr(0, last_dot)) != scopes_.end()) {
        continue;
      }
      render_.addScope(scope);
    }
  }

  void addScope(const Stats::Scope& scope, const std::string& scope_name) {
    if (scope_name != params_.scope_ && (params_.matchesAny() || hasAny(scope))) {
      scopes_.insert(scope_name);
    }
  }

  Render& render() { return render_; }

  const Params& params_;
  Render& render_;
  Buffer::Instance& response_;

  SharedStatSet<Stats::Counter> counters_;
  SharedStatSet<Stats::Gauge> gauges_;
  SharedStatSet<Stats::TextReadout> text_readouts_;
  SharedStatSet<Stats::Histogram> histograms_;
  std::set<std::string> scopes_;
};

Http::Code StatsHandler::stats(const Params& params, Stats::Store& stats,
                               Http::ResponseHeaderMap& response_headers,
                               Buffer::Instance& response) {
  std::unique_ptr<Render> render;

  switch (params.format_) {
    case Format::Html:
      render = std::make_unique<HtmlRender>(response_headers, response, *this, params);
      break;
    case Format::Json:
      render = std::make_unique<JsonRender>(params);
      response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
      break;
    case Format::Prometheus:
      ASSERT(false);
      ABSL_FALLTHROUGH_INTENDED;
    case Format::Text:
      render = std::make_unique<TextRender>();
      break;
  }

  // The default HTML view, with no scope query-param specified (or an empty string), we will
  // just generate some HTML to initiate a JSON request to populate the scopes. This way the
  // rendering for scopes and sub-scopes can be handled consistently in JavaScript.
  if (params.scope_.empty() && params.format_ == Format::Html) {
    StatsHandler::HtmlRender::renderAjaxRequestForScopes(response, params);
  } else {
    Context context(params, *render, response);

    // Note that multiple scopes can exist with the same name, and also that
    // scopes may be created/destroyed in other threads, whenever we are not
    // holding the store's lock. So when we traverse the scopes, we'll both
    // look for Scope objects matching our expected name, and we'll create
    // a sorted list of sort names for use in populating next/previous buttons.
    auto scope_fn = [this, &params, &context](const Stats::Scope& scope) {
      std::string prefix_str = server_.stats().symbolTable().toString(scope.prefix());

      if (params.query_.find("show_json_scopes") != params.query_.end()) {
        if (params.scope_ == prefix_str) {
          context.collectScope(scope);
        } else if (params.scope_.empty() ||
                   absl::StartsWith(prefix_str + ".", params.scope_ + ".")) {
          // Truncate any hierarchy after the prefix.
          size_t dot_search = params.scope_.empty() ? 0 : params.scope_.size() + 1;
          size_t dot = prefix_str.find('.', dot_search);
          if (dot != std::string::npos) {
            prefix_str.resize(dot);
          }
          context.addScope(scope, prefix_str);
        }
      } else if (prefix_str == params.scope_ || prefix_str.empty() || params.scope_.empty()) {
        // If the scope matches the prefix of what the user wants, append in the
        // stats from it. Note that scopes with a prefix of "" will match anything
        // the user types, in which case we'll still be filtering based on stat
        // name prefix.
        context.collectScope(scope);
      } else if (absl::StartsWith(prefix_str, params.scope_) &&
                 prefix_str[params.scope_.size()] == '.') {
        context.addScope(scope, prefix_str);
      }
    };
    stats.forEachScope([](size_t) {}, scope_fn);
    context.emit();
    render->render(response);
  }

  if (params.format_ == Format::Html) {
    response.add("</body>\n");
  }

  // Display plain stats if format query param is not there.
  // statsAsText(counters_and_gauges, text_readouts, histograms, response);
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsPrometheus(absl::string_view path_and_query,
                                                Http::ResponseHeaderMap&,
                                                Buffer::Instance& response, AdminStream&) {
  Params params;
  Http::Code code = params.parse(path_and_query, response);
  if (code != Http::Code::OK) {
    return code;
  }
  if (server_.statsConfig().flushOnAdmin()) {
    server_.flushStats();
  }
  Stats::Store& stats = server_.stats();
  const std::vector<Stats::TextReadoutSharedPtr>& text_readouts_vec =
      params.prometheus_text_readouts_ ? stats.textReadouts()
      : std::vector<Stats::TextReadoutSharedPtr>();
  PrometheusStatsFormatter::statsAsPrometheus(stats.counters(), stats.gauges(), stats.histograms(),
                                              text_readouts_vec, response, params.used_only_,
                                              params.filter_, server_.api().customStatNamespaces());
  Admin::RequestPtr StatsHandler::makeRequest(absl::string_view path, AdminStream &
                                              /*admin_stream*/) {
    StatsParams params;
    Buffer::OwnedImpl response;
    Http::Code code = params.parse(path, response);
    if (code != Http::Code::OK) {
      return Admin::makeStaticTextRequest(response, code);
    }

    if (params.format_ == StatsFormat::Prometheus) {
      // TODO(#16139): modify streaming algorithm to cover Prometheus.
      //
      // This may be easiest to accomplish by populating the set
      // with tagExtractedName(), and allowing for vectors of
      // stats as multiples will have the same tag-extracted names.
      // Ideally we'd find a way to do this without slowing down
      // the non-Prometheus implementations.
      Buffer::OwnedImpl response;
      prometheusFlushAndRender(params, response);
      return Admin::makeStaticTextRequest(response, code);
    }

    if (server_.statsConfig().flushOnAdmin()) {
      server_.flushStats();
    }

    return makeRequest(server_.stats(), params,
                       [this]() -> Admin::UrlHandler { return statsHandler(); });
  }

  Admin::RequestPtr StatsHandler::makeRequest(Stats::Store & stats, const StatsParams& params,
                                              StatsRequest::UrlHandlerFn url_handler_fn) {
    return std::make_unique<StatsRequest>(stats, params, url_handler_fn);
  }

  Http::Code StatsHandler::handlerPrometheusStats(absl::string_view path_and_query,
                                                  Http::ResponseHeaderMap&,
                                                  Buffer::Instance & response, AdminStream&) {
    return prometheusStats(path_and_query, response);
  }

  Http::Code StatsHandler::prometheusStats(absl::string_view path_and_query,
                                           Buffer::Instance & response) {
    StatsParams params;
    Http::Code code = params.parse(path_and_query, response);
    if (code != Http::Code::OK) {
      return code;
    }

    if (server_.statsConfig().flushOnAdmin()) {
      server_.flushStats();
    }

    prometheusFlushAndRender(params, response);
    return Http::Code::OK;
  }

  void StatsHandler::prometheusFlushAndRender(const StatsParams& params,
                                              Buffer::Instance& response) {
    if (server_.statsConfig().flushOnAdmin()) {
      server_.flushStats();
    }
    prometheusRender(server_.stats(), server_.api().customStatNamespaces(), params, response);
  }

  void StatsHandler::prometheusRender(Stats::Store & stats,
                                      const Stats::CustomStatNamespaces& custom_namespaces,
                                      const StatsParams& params, Buffer::Instance& response) {
    const std::vector<Stats::TextReadoutSharedPtr>& text_readouts_vec =
        params.prometheus_text_readouts_ ? stats.textReadouts()
        : std::vector<Stats::TextReadoutSharedPtr>();
    PrometheusStatsFormatter::statsAsPrometheus(stats.counters(), stats.gauges(),
                                                stats.histograms(), text_readouts_vec, response,
                                                params, custom_namespaces);
  }

  Http::Code StatsHandler::handlerContention(absl::string_view,
                                             Http::ResponseHeaderMap & response_headers,
                                             Buffer::Instance & response, AdminStream&) {

    if (server_.options().mutexTracingEnabled() && server_.mutexTracer() != nullptr) {
      response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);

      envoy::admin::v3::MutexStats mutex_stats;
      mutex_stats.set_num_contentions(server_.mutexTracer()->numContentions());
      mutex_stats.set_current_wait_cycles(server_.mutexTracer()->currentWaitCycles());
      mutex_stats.set_lifetime_wait_cycles(server_.mutexTracer()->lifetimeWaitCycles());
      response.add(MessageUtil::getJsonStringFromMessageOrError(mutex_stats, true, true));
    } else {
      response.add("Mutex contention tracing is not enabled. To enable, run Envoy with flag "
                   "--enable-mutex-tracing.");
    }
    return Http::Code::OK;
  }

  Admin::UrlHandler StatsHandler::statsHandler() {
    return {
      "/stats",
      "print server stats",
      [this](absl::string_view path, AdminStream& admin_stream) -> Admin::RequestPtr {
        return makeRequest(path, admin_stream);
      },
      false,
      false,
      {{Admin::ParamDescriptor::Type::Boolean, "usedonly",
         "Only include stats that have been written by system since restart"},
       {Admin::ParamDescriptor::Type::String, "filter",
        "Regular expression (ecmascript) for filtering stats"},
       {Admin::ParamDescriptor::Type::Enum, "format", "Format to use", {"html", "text", "json"}},
       {Admin::ParamDescriptor::Type::Enum,
        "type",
        "Stat types to include.",
        {StatLabels::All, StatLabels::Counters, StatLabels::Histograms, StatLabels::Gauges,
         StatLabels::TextReadouts}},
       {Admin::ParamDescriptor::Type::Enum,
        "histogram_buckets",
        "Histogram bucket display mode",
        {"cumulative", "disjoint", "none"}}}};
  }
};

} // namespace Server
} // namespace Envoy
