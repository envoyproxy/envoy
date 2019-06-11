#include "common/router/scoped_config_impl.h"

namespace Envoy {
namespace Router {

std::unique_ptr<ScopeKeyFragmentBase>
HeaderValueExtractorImpl::computeFragment(const Http::HeaderMap& headers) const {
  const Envoy::Http::HeaderEntry* header_entry =
      headers.get(Envoy::Http::LowerCaseString(header_value_extractor_config_.name()));
  if (header_entry == nullptr) {
    return nullptr;
  }

  std::vector<absl::string_view> elements{header_entry->value().getStringView()};
  if (header_value_extractor_config_.element_separator().length() > 0) {
    elements = absl::StrSplit(header_entry->value().getStringView(),
                              header_value_extractor_config_.element_separator());
  }
  switch (header_value_extractor_config_.extract_type_case()) {
  case ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::HeaderValueExtractor::kElement:
    for (const auto& element : elements) {
      std::pair<absl::string_view, absl::string_view> key_value = absl::StrSplit(
          element, absl::MaxSplits(header_value_extractor_config_.element().separator(), 1));
      if (key_value.first == header_value_extractor_config_.element().key()) {
        return std::make_unique<StringKeyFragment>(key_value.second);
      }
    }
    break;
  case ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::HeaderValueExtractor::kIndex:
    if (header_value_extractor_config_.index() < elements.size()) {
      return std::make_unique<StringKeyFragment>(elements[header_value_extractor_config_.index()]);
    }
    break;
  default:                       // EXTRACT_TYPE_NOT_SET
    NOT_REACHED_GCOVR_EXCL_LINE; // Caught in constructor already.
  }

  return nullptr;
}

ScopeKeyBuilderImpl::ScopeKeyBuilderImpl(ScopedRoutes::ScopeKeyBuilder config)
    : ScopeKeyBuilderBase(config) {
  for (const auto& fragment_builder : config_.fragments()) {
    switch (fragment_builder.type_case()) {
    case ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::kHeaderValueExtractor:
      fragment_builders_.emplace_back(std::make_unique<HeaderValueExtractorImpl>(fragment_builder));
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }
}

const ScopeKey ScopeKeyBuilderImpl::computeScopeKey(const Http::HeaderMap& headers) const {
  ScopeKey key;
  for (const auto& builder : fragment_builders_) {
    key.addFragment(builder->computeFragment(headers));
  }
  return key;
}

void ThreadLocalScopedConfigImpl::addOrUpdateRoutingScope(const ScopedRouteInfoConstSharedPtr&) {}

void ThreadLocalScopedConfigImpl::removeRoutingScope(const std::string&) {}

Router::ConfigConstSharedPtr
ThreadLocalScopedConfigImpl::getRouteConfig(const Http::HeaderMap&) const {
  return std::make_shared<const NullConfigImpl>();
}

} // namespace Router
} // namespace Envoy
