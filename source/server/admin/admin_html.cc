#include "envoy/server/admin_handler_factory.h"

#include "source/common/html/utility.h"
#include "source/server/admin/admin.h"
#include "source/server/admin/stats_html_render.h"

namespace Envoy {
namespace Server {

class AdminHandlerHome : public AdminHandler {
public:
  AdminHandlerHome(AdminImpl& admin) : admin_(admin) {}
  const std::string& prefix() const override {
    static const std::string prefix = "/";
    return prefix;
  }
  const std::string& helpText() const override {
    static const std::string help_text = "Admin home page";
    return help_text;
  }
  bool removable() const override { return false; }
  bool mutatesServerState() const override { return false; }
  const Admin::ParamDescriptorVec& params() const override {
    static const Admin::ParamDescriptorVec& params{};
    return params;
  }
  Http::Code runCallback(Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
                         AdminStream&) override {
    StatsHtmlRender html(response_headers, response, StatsParams());
    html.tableBegin(response);

    // Prefix order is used during searching, but for printing do them in alpha order.
    OptRef<const Http::Utility::QueryParams> no_query_params;
    for (const Admin::UrlHandler* handler : admin_.sortedHandlers()) {
      html.urlHandler(response, *handler, no_query_params);
    }

    html.tableEnd(response);
    html.finalize(response);

    return Http::Code::OK;
  }

private:
  AdminImpl& admin_;
};

class AdminHandlerHomeFactory : public AdminHandlerFactory {
public:
  std::unique_ptr<AdminHandler> createHandler(Admin& admin, Server::Instance&) const override {
    return std::make_unique<AdminHandlerHome>(dynamic_cast<AdminImpl&>(admin));
  }
  std::string name() const override { return "home"; }
};

REGISTER_FACTORY(AdminHandlerHomeFactory, AdminHandlerFactory);

} // namespace Server
} // namespace Envoy
