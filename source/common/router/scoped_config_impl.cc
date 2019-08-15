#include "common/router/scoped_config_impl.h"

namespace Envoy {
namespace Router {

bool ScopeKey::operator!=(const ScopeKey& other) const { return !(*this == other); }

bool ScopeKey::operator==(const ScopeKey& other) const {
  if (fragments_.empty() || other.fragments_.empty()) {
    // An empty key equals to nothing, "NULL" != "NULL".
    return false;
  }
  return std::equal(fragments_.begin(), fragments_.end(), other.fragments_.begin(),
                    other.fragments_.end(),
                    [](const std::unique_ptr<ScopeKeyFragmentBase>& left,
                       const std::unique_ptr<ScopeKeyFragmentBase>& right) -> bool {
                      // Both should be non-NULL now.
                      return *left == *right;
                    });
}

HeaderValueExtractorImpl::HeaderValueExtractorImpl(
    ScopedRoutes::ScopeKeyBuilder::FragmentBuilder&& config)
    : FragmentBuilderBase(std::move(config)),
      header_value_extractor_config_(config_.header_value_extractor()) {
  ASSERT(config_.type_case() ==
             ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::kHeaderValueExtractor,
         "header_value_extractor is not set.");
  if (header_value_extractor_config_.extract_type_case() ==
      ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::HeaderValueExtractor::kIndex) {
    if (header_value_extractor_config_.index() != 0 &&
        header_value_extractor_config_.element_separator().empty()) {
      throw ProtoValidationException("Index > 0 for empty string element separator.",
                                     header_value_extractor_config_);
    }
  }
  if (header_value_extractor_config_.extract_type_case() ==
      ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::HeaderValueExtractor::EXTRACT_TYPE_NOT_SET) {
    throw ProtoValidationException("HeaderValueExtractor extract_type not set.",
                                   header_value_extractor_config_);
  }
}

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

ScopeKeyBuilderImpl::ScopeKeyBuilderImpl(ScopedRoutes::ScopeKeyBuilder&& config)
    : ScopeKeyBuilderBase(std::move(config)) {
  for (const auto& fragment_builder : config_.fragments()) {
    switch (fragment_builder.type_case()) {
    case ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::kHeaderValueExtractor:
      fragment_builders_.emplace_back(std::make_unique<HeaderValueExtractorImpl>(
          ScopedRoutes::ScopeKeyBuilder::FragmentBuilder(fragment_builder)));
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }
}

std::unique_ptr<ScopeKey>
ScopeKeyBuilderImpl::computeScopeKey(const Http::HeaderMap& headers) const {
  ScopeKey key;
  for (const auto& builder : fragment_builders_) {
    // returns nullopt if a null fragment is found.
    std::unique_ptr<ScopeKeyFragmentBase> fragment = builder->computeFragment(headers);
    if (fragment == nullptr) {
      return nullptr;
    }
    key.addFragment(std::move(fragment));
  }
  return std::make_unique<ScopeKey>(std::move(key));
}

void ScopedConfigImpl::addOrUpdateRoutingScope(const ScopedRouteInfoConstSharedPtr&) {}

void ScopedConfigImpl::removeRoutingScope(const std::string&) {}

Router::ConfigConstSharedPtr ScopedConfigImpl::getRouteConfig(const Http::HeaderMap&) const {
  return std::make_shared<const NullConfigImpl>();
}

} // namespace Router
} // namespace Envoy
