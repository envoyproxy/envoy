#pragma once

#include "envoy/filesystem/filesystem.h"

#include "source/server/admin/admin_html_util.h"
#include "source/server/admin/stats_render.h"

namespace Envoy {
namespace Server {

class StatsHtmlRender : public StatsTextRender {
public:
  StatsHtmlRender(Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
                  const StatsParams& params);

  /**
   * Writes the header and starts the body for a stats page based on the
   * supplied stats parameters.
   *
   * @param url_handler The stats URL handler.
   * @param params The parameters for the stats page.
   * @param response The buffer in which to write the HTML.
   */
  void setupStatsPage(const Admin::UrlHandler& url_handler, const StatsParams& params,
                      Buffer::Instance& response);

  // StatsTextRender
  void noStats(Buffer::Instance&, absl::string_view types) override;
  void generate(Buffer::Instance& response, const std::string& name,
                const std::string& value) override;

  // This matches the superclass impl exactly, but is needed to allow gcc to compile, which
  // warns about hidden overrides if we omit it.
  void generate(Buffer::Instance& response, const std::string& name, uint64_t value) override {
    StatsTextRender::generate(response, name, value);
  }

  void generate(Buffer::Instance&, const std::string& name,
                const Stats::ParentHistogram& histogram) override;
  void finalize(Buffer::Instance&) override;

private:
  const bool active_{false};
  bool json_histograms_{false};
  bool first_histogram_{true};
};

} // namespace Server
} // namespace Envoy
