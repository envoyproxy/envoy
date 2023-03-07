#pragma once

#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/server/admin.h"
#include "envoy/server/instance.h"

namespace Envoy {
namespace Server {

class AdminHandler {
public:
  virtual Http::Code runCallback(Http::ResponseHeaderMap& response_headers,
                                 Buffer::Instance& response, AdminStream& admin_stream) PURE;
  virtual const std::string& prefix() const PURE;
  virtual const std::string& helpText() const PURE;
  Admin::HandlerCb callback() { return MAKE_ADMIN_HANDLER(runCallback); }
  virtual bool removable() const PURE;
  virtual bool mutatesServerState() const PURE;
  virtual const Admin::ParamDescriptorVec& params() const PURE;

  virtual ~AdminHandler() = default;
};

class AdminHandlerFactory : public Config::UntypedFactory {
public:
  virtual std::unique_ptr<AdminHandler> createHandler(Admin& admin,
                                                      Server::Instance& server) const PURE;
  std::string category() const override { return "envoy.admin_handlers"; }
};

} // namespace Server
} // namespace Envoy
