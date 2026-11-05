#include "source/common/router/scoped_config_impl.h"

#include "envoy/config/route/v3/scoped_route.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"

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
      ProtoExceptionUtil::throwProtoValidationException(
          "Index > 0 for empty string element separator.", config_);
    }
  }
  if (header_value_extractor_config_.extract_type_case() ==
      ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::HeaderValueExtractor::EXTRACT_TYPE_NOT_SET) {
    ProtoExceptionUtil::throwProtoValidationException("HeaderValueExtractor extract_type not set.",
                                                      config_);
  }
}

std::unique_ptr<ScopeKeyFragmentBase>
HeaderValueExtractorImpl::computeFragment(const Http::HeaderMap& headers) const {
  const auto header_entry =
      headers.get(Envoy::Http::LowerCaseString(header_value_extractor_config_.name()));
  if (header_entry.empty()) {
    return nullptr;
  }

  // This is an implicitly untrusted header, so per the API documentation only the first
  // value is used.
  std::vector<absl::string_view> elements{header_entry[0]->value().getStringView()};
  if (!header_value_extractor_config_.element_separator().empty()) {
    elements = absl::StrSplit(header_entry[0]->value().getStringView(),
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
  case ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::HeaderValueExtractor::EXTRACT_TYPE_NOT_SET:
    PANIC("not reached"); // LCOV_EXCL_LINE
  }

  return nullptr;
}

ScopedRouteInfo::ScopedRouteInfo(envoy::config::route::v3::ScopedRouteConfiguration&& config_proto,
                                 ConfigConstSharedPtr route_config)
    : config_proto_(std::move(config_proto)), route_config_(route_config),
      config_hash_(MessageUtil::hash(config_proto_)) {
  // TODO(stevenzzzz): Maybe worth a KeyBuilder abstraction when there are more than one type of
  // Fragment.
  using Fragment = envoy::config::route::v3::ScopedRouteConfiguration::Key::Fragment;
  for (const auto& fragment : config_proto_.key().fragments()) {
    switch (fragment.type_case()) {
    case Fragment::TypeCase::kStringKey:
      scope_key_.addFragment(std::make_unique<StringKeyFragment>(fragment.string_key()));
      break;
    case Fragment::TypeCase::TYPE_NOT_SET: // LCOV_EXCL_LINE
      PANIC("not implemented");            // LCOV_EXCL_LINE
    }
  }
}

ScopeKeyBuilderImpl::ScopeKeyBuilderImpl(ScopedRoutes::ScopeKeyBuilder&& config)
    : ScopeKeyBuilderBase(std::move(config)) {
  fragment_builders_.reserve(config_.fragments().size());
  for (const auto& fragment_builder : config_.fragments()) {
    switch (fragment_builder.type_case()) {
    case ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::kHeaderValueExtractor:
      fragment_builders_.emplace_back(std::make_unique<HeaderValueExtractorImpl>(
          ScopedRoutes::ScopeKeyBuilder::FragmentBuilder(fragment_builder)));
      break;
    case ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::TYPE_NOT_SET: // LCOV_EXCL_LINE
      PANIC("not implemented");                                        // LCOV_EXCL_LINE
    }
  }
}

ScopeKeyPtr ScopeKeyBuilderImpl::computeScopeKey(const Http::HeaderMap& headers) const {
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

void ScopedConfigImpl::addOrUpdateRoutingScopes(
    const std::vector<ScopedRouteInfoConstSharedPtr>& scoped_route_infos) {
  if (scoped_route_infos.size() > 1 &&
      Runtime::runtimeFeatureEnabled("envoy.reloadable_features.srds_fast_config_update")) {
    size_t net_added = 0;
    if (scoped_route_info_by_name_.empty()) {
      net_added = scoped_route_infos.size();
    } else {
      for (const auto& scoped_route_info : scoped_route_infos) {
        if (!scoped_route_info_by_name_.contains(scoped_route_info->scopeName())) {
          ++net_added;
        }
      }
    }
    // Reserves sufficient space for the new scoped route info; this avoids repeated memory
    // allocation and rehashing while updating the maps.
    if (net_added > 0) {
      scoped_route_info_by_name_.reserve(scoped_route_info_by_name_.size() + net_added);
      scoped_route_info_by_key_.reserve(scoped_route_info_by_key_.size() + net_added);
    }
  }
  for (auto& scoped_route_info : scoped_route_infos) {
    const auto iter = scoped_route_info_by_name_.find(scoped_route_info->scopeName());
    if (iter != scoped_route_info_by_name_.end()) {
      ASSERT(scoped_route_info_by_key_.contains(iter->second->scopeKey().hash()));
      scoped_route_info_by_key_.erase(iter->second->scopeKey().hash());
      // Explicitly erase from _name_ map to avoid dangling string_view on overwrite.
      scoped_route_info_by_name_.erase(iter);
    }
    scoped_route_info_by_name_.emplace(scoped_route_info->scopeName(), scoped_route_info);
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

void ScopedConfigImpl::onConfigUpdate(
    const std::vector<ScopedRouteInfoConstSharedPtr>& added_or_updated_scopes,
    const std::vector<std::string>& removed_scope_names) {
  // Process removals before additions in case a removed scope and a newly added scope
  // share the same ScopeKey hash.
  removeRoutingScopes(removed_scope_names);
  addOrUpdateRoutingScopes(added_or_updated_scopes);
}

Router::ConfigConstSharedPtr ScopedConfigImpl::getRouteConfig(const ScopeKeyPtr& scope_key) const {
  if (scope_key == nullptr) {
    return nullptr;
  }
  auto iter = scoped_route_info_by_key_.find(scope_key->hash());
  if (iter != scoped_route_info_by_key_.end()) {
    return iter->second->routeConfig();
  }
  return nullptr;
}

} // namespace Router
} // namespace Envoy
