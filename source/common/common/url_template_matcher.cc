#include "common/common/url_template_matcher.h"

#include "common/common/empty_string.h"
#include "common/common/utility.h"

namespace Envoy {
namespace Matchers {

namespace {

// Http method for all url_template matches.
// This could be anything.
constexpr absl::string_view HTTP_METHOD{"GET"};

} // namespace

UrlTemplateMatcher::UrlTemplateMatcher(const absl::string_view url_template) {
  google::grpc::transcoding::PathMatcherBuilder<const UrlTemplateMatcher*> pmb;
  auto ok = pmb.Register(std::string(HTTP_METHOD), std::string(url_template), EMPTY_STRING, this);
  if (!ok) {
    ExceptionUtil::throwEnvoyException(fmt::format("invalid url_template: {}", url_template));
  }
  path_matcher_ = pmb.Build();
}

bool UrlTemplateMatcher::match(const absl::string_view path) const {
  return path_matcher_->Lookup(std::string(HTTP_METHOD), std::string(path)) != nullptr;
}

} // namespace Matchers
} // namespace Envoy
