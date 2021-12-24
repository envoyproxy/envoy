#pragma once

#include "envoy/common/optref.h"
#include "envoy/server/admin.h"

#include "source/common/buffer/buffer_impl.h"

#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Server {

class AdminHtmlGenerator {
public:
  AdminHtmlGenerator(Buffer::Instance& response) : response_(response) {}

  void renderHead();
  void renderTail();
  void renderUrlHandler(const Admin::UrlHandler& handler,
                        OptRef<const Http::Utility::QueryParams> query);
  void renderInput(absl::string_view id, absl::string_view path, Admin::ParamDescriptor::Type type,
                   OptRef<const Http::Utility::QueryParams> query,
                   const std::vector<std::string>& enum_choices);

private:
  Buffer::Instance& response_;
  int index_{0};
};

} // namespace Server
} // namespace Envoy
