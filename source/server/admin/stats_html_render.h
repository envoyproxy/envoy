#pragma once

#include "source/server/admin/admin_html_generator.h"
#include "source/server/admin/stats_render.h"

namespace Envoy {
namespace Server {

class StatsHtmlRender : public StatsTextRender {
public:
  StatsHtmlRender(Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
                  const Admin::UrlHandler& url_handler, const StatsParams& params);

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

private:
  AdminHtmlGenerator html_;
};

} // namespace Server
} // namespace Envoy
