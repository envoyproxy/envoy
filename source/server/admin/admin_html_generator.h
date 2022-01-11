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
  void renderTableBegin();
  void renderTableEnd();
  void renderUrlHandler(const Admin::UrlHandler& handler,
                        OptRef<const Http::Utility::QueryParams> query);
  void renderInput(absl::string_view id, absl::string_view path, Admin::ParamDescriptor::Type type,
                   OptRef<const Http::Utility::QueryParams> query,
                   const std::vector<absl::string_view>& enum_choices);
  void setSubmitOnChange(bool submit_on_change) { submit_on_change_ = submit_on_change; }
  void setVisibleSubmit(bool visible_submit) { visible_submit_ = visible_submit; }

private:
  Buffer::Instance& response_;
  int index_{0};
  bool submit_on_change_{false};
  bool visible_submit_{true};
};

} // namespace Server
} // namespace Envoy
