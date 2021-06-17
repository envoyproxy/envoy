#include "source/common/router/scoped_config_impl.h"

#include "envoy/config/route/v3/scoped_route.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

namespace Envoy {
namespace Router {

bool ScopeKey::operator!=(const ScopeKey& other) const { return !(*this == other); }

bool ScopeKey::operator==(const ScopeKey& other) const {
  if (fragments_.empty() || other.fragments_.empty()) {
    // An empty key equals to nothing, "NULL" != "NULL".
    return false;
  }
  return this->hash() == other.hash();
}

void FragmentBuilderImpl::validateHeaderValueExtractorConfig(
    const HeaderValueExtractorConfig& config) const {
  ASSERT(config_.type_case() ==
             ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::kHeaderValueExtractor,
         "header_value_extractor is not set.");

  if (config.extract_type_case() == HeaderValueExtractorConfig::kIndex) {
    if (config.index() != 0 &&
        config.element_separator().empty()) {
      throw ProtoValidationException("Index > 0 for empty string element separator.",
                                     config);
    }
  }

  if (config.extract_type_case() ==
      HeaderValueExtractorConfig::EXTRACT_TYPE_NOT_SET) {
    throw ProtoValidationException("HeaderValueExtractor extract_type not set.",
                                   config);
  }
}

void FragmentBuilderImpl::validateMetadataValueExtractorConfig(
    const MetadataValueExtractorConfig&) const { /* @tallen */ }

FragmentBuilderImpl::FragmentBuilderImpl(ScopedRoutes::ScopeKeyBuilder::FragmentBuilder&& config)
    : FragmentBuilderBase(std::move(config)) {
  switch (config_.type_case()) {
  case FragmentBuilderConfig::kHeaderValueExtractor:
    validateHeaderValueExtractorConfig(config.header_value_extractor());
    break;
  case FragmentBuilderConfig::kMetadataValueExtractor:
    validateMetadataValueExtractorConfig(config.metadata_value_extractor());
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE; // Caught in constructor already.
  }
}

std::unique_ptr<ScopeKeyFragmentBase>
FragmentBuilderImpl::computeFragment(const Http::HeaderMap& headers, const Metadata& meta) const {
  switch (config_.type_case()) {
  case FragmentBuilderConfig::kHeaderValueExtractor:
    return computeFragmentFromHeader(headers, config_.header_value_extractor());
  case FragmentBuilderConfig::kMetadataValueExtractor:
    return computeFragmentFromMetadata(meta, config_.metadata_value_extractor());
  default:
    NOT_REACHED_GCOVR_EXCL_LINE; // Caught in constructor already.
  }
  return nullptr;
}

std::unique_ptr<ScopeKeyFragmentBase> FragmentBuilderImpl::computeFragmentFromHeader(
    const Http::HeaderMap& headers,
    const ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::HeaderValueExtractor&
        header_value_extractor_config) const {

  const auto header_entry =
      headers.get(Envoy::Http::LowerCaseString(header_value_extractor_config.name()));
  if (header_entry.empty()) {
    return nullptr;
  }

  // This is an implicitly untrusted header, so per the API documentation only the first
  // value is used.
  std::vector<absl::string_view> elements{header_entry[0]->value().getStringView()};
  if (header_value_extractor_config.element_separator().length() > 0) {
    elements = absl::StrSplit(header_entry[0]->value().getStringView(),
                              header_value_extractor_config.element_separator());
  }
  switch (header_value_extractor_config.extract_type_case()) {
  case ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::HeaderValueExtractor::kElement:
    for (const auto& element : elements) {
      std::pair<absl::string_view, absl::string_view> key_value = absl::StrSplit(
          element, absl::MaxSplits(header_value_extractor_config.element().separator(), 1));
      if (key_value.first == header_value_extractor_config.element().key()) {
        return std::make_unique<StringKeyFragment>(key_value.second);
      }
    }
    break;
  case ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::HeaderValueExtractor::kIndex:
    if (header_value_extractor_config.index() < elements.size()) {
      return std::make_unique<StringKeyFragment>(elements[header_value_extractor_config.index()]);
    }
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  return nullptr;
}

std::unique_ptr<ScopeKeyFragmentBase>
FragmentBuilderImpl::computeFragmentFromMetadata(const Metadata& /*meta*/,
                              const MetadataValueExtractorConfig& /*config*/) const {
  ASSERT(false); // @tallen
  return nullptr;
}

ScopedRouteInfo::ScopedRouteInfo(envoy::config::route::v3::ScopedRouteConfiguration&& config_proto,
                                 ConfigConstSharedPtr&& route_config)
    : config_proto_(std::move(config_proto)), route_config_(std::move(route_config)) {
  // TODO(stevenzzzz): Maybe worth a KeyBuilder abstraction when there are more than one type of
  // Fragment.
  for (const auto& fragment : config_proto_.key().fragments()) {
    switch (fragment.type_case()) {
    case envoy::config::route::v3::ScopedRouteConfiguration::Key::Fragment::TypeCase::kStringKey:
      scope_key_.addFragment(std::make_unique<StringKeyFragment>(fragment.string_key()));
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }
}

ScopeKeyBuilderImpl::ScopeKeyBuilderImpl(ScopedRoutes::ScopeKeyBuilder&& config)
    : ScopeKeyBuilderBase(std::move(config)) {
  for (const auto& fragment_builder : config_.fragments()) {
    switch (fragment_builder.type_case()) {
    case ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::kHeaderValueExtractor:
    case ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::kMetadataValueExtractor:
      fragment_builders_.emplace_back(std::make_unique<FragmentBuilderImpl>(
          ScopedRoutes::ScopeKeyBuilder::FragmentBuilder(fragment_builder)));
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }
}

ScopeKeyPtr
ScopeKeyBuilderImpl::computeScopeKey(const Http::HeaderMap& headers,
                                     const envoy::config::core::v3::Metadata& meta) const {
  ScopeKey key;
  for (const auto& builder : fragment_builders_) {
    // returns nullopt if a null fragment is found.
    std::unique_ptr<ScopeKeyFragmentBase> fragment = builder->computeFragment(headers, meta);

    if (fragment == nullptr) {
      return nullptr;
    }
    key.addFragment(std::move(fragment));
  }
  return std::make_unique<ScopeKey>(std::move(key));
}

void ScopedConfigImpl::addOrUpdateRoutingScopes(
    const std::vector<ScopedRouteInfoConstSharedPtr>& scoped_route_infos) {
  for (auto& scoped_route_info : scoped_route_infos) {
    const auto iter = scoped_route_info_by_name_.find(scoped_route_info->scopeName());
    if (iter != scoped_route_info_by_name_.end()) {
      ASSERT(scoped_route_info_by_key_.contains(iter->second->scopeKey().hash()));
      scoped_route_info_by_key_.erase(iter->second->scopeKey().hash());
    }
    scoped_route_info_by_name_[scoped_route_info->scopeName()] = scoped_route_info;
    scoped_route_info_by_key_[scoped_route_info->scopeKey().hash()] = scoped_route_info;
  }
}

void ScopedConfigImpl::removeRoutingScopes(const std::vector<std::string>& scope_names) {
  for (std::string const& scope_name : scope_names) {
    const auto iter = scoped_route_info_by_name_.find(scope_name);
    if (iter != scoped_route_info_by_name_.end()) {
      ASSERT(scoped_route_info_by_key_.contains(iter->second->scopeKey().hash()));
      scoped_route_info_by_key_.erase(iter->second->scopeKey().hash());
      scoped_route_info_by_name_.erase(iter);
    }
  }
}

Router::ConfigConstSharedPtr
ScopedConfigImpl::getRouteConfig(const Http::HeaderMap& headers, const envoy::config::core::v3::Metadata& meta) const {
  ScopeKeyPtr scope_key = scope_key_builder_.computeScopeKey(headers, meta);
  if (scope_key == nullptr) {
    return nullptr;
  }
  auto iter = scoped_route_info_by_key_.find(scope_key->hash());
  if (iter != scoped_route_info_by_key_.end()) {
    return iter->second->routeConfig();
  }
  return nullptr;
}

ScopeKeyPtr ScopedConfigImpl::computeScopeKey(const Http::HeaderMap& headers, const envoy::config::core::v3::Metadata& meta) const {
  ScopeKeyPtr scope_key = scope_key_builder_.computeScopeKey(headers, meta);
  if (scope_key &&
      scoped_route_info_by_key_.find(scope_key->hash()) != scoped_route_info_by_key_.end()) {
    return scope_key;
  }
  return nullptr;
}

} // namespace Router
} // namespace Envoy
