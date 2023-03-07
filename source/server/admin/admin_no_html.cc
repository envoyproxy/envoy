#include "envoy/server/admin_handler_factory.h"

namespace Envoy {
namespace Server {

class AdminHandlerHome : public AdminHandler {
public:
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
    response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Text);
    response.add("HTML output was disabled by building with --define=admin_html=disabled");
    return Http::Code::OK;
  }
};

class AdminHandlerHomeFactory : public AdminHandlerFactory {
public:
  std::unique_ptr<AdminHandler> createHandler(Admin&, Server::Instance&) const override {
    return std::make_unique<AdminHandlerHome>();
  }
  std::string name() const override { return "home"; }
};

REGISTER_FACTORY(AdminHandlerHomeFactory, AdminHandlerFactory);

} // namespace Server
} // namespace Envoy
