#pragma once

#include "source/server/admin/stats_render.h"

namespace Envoy {
namespace Server {

class StatsHtmlRender : public StatsTextRender {
public:
  StatsHtmlRender(Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
                  const StatsParams& params);

  void noStats(Buffer::Instance&, absl::string_view types) override;
  void finalize(Buffer::Instance&) override;
  void generate(Buffer::Instance& response, const std::string& name,
                const std::string& value) override;
  void generate(Buffer::Instance& response, const std::string& name, uint64_t value) override {
    StatsTextRender::generate(response, name, value);
  }
  void generate(Buffer::Instance& response, const std::string& name,
                const Stats::ParentHistogram& histogram) override {
    StatsTextRender::generate(response, name, histogram);
  }

  /**
   * Renders the HTML head into the response buffer provided in the constructor.
   */
  void renderHead();

  /**
   * Renders the beginning of the help-table into the response buffer provided
   * in the constructor.
   */
  void renderTableBegin();

  /**
   * Renders the end of the help-table into the response buffer provided in the
   * constructor.
   */
  void renderTableEnd();

  /**
   * Renders a table row for a URL endpoint, including the name of the endpoint,
   * entries for each parameter, and help text.
   *
   * This must be called after renderTableBegin and before renderTableEnd. Any
   * number of URL Handlers can be rendered.
   *
   * @param handler the URL handler.
   */
  void renderUrlHandler(const Admin::UrlHandler& handler,
                        OptRef<const Http::Utility::QueryParams> query);

  void renderInput(absl::string_view id, absl::string_view path, Admin::ParamDescriptor::Type type,
                   OptRef<const Http::Utility::QueryParams> query,
                   const std::vector<absl::string_view>& enum_choices);

  // By default, editing parameters does not cause a form-submit -- you have
  // to click on the link or button first. This is useful for the admin home
  // page which lays out all the parameters so users can tweak them before submitting.
  //
  // Calling setSubmitOnChange(true) makes the form auto-submits when any
  // parameters change, and does not have its own explicit submit button. This
  // is used to enable the user to adjust query-parameters while visiting an
  // html-rendered endpoint.
  void setSubmitOnChange(bool submit_on_change) { submit_on_change_ = submit_on_change; }

private:
  Buffer::Instance& response_;
  int index_{0}; // Used to alternate row-group background color
  bool submit_on_change_{false};
};

} // namespace Server
} // namespace Envoy
