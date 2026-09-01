#include "source/extensions/http/early_header_mutation/exact_path_rewrite/header_mutation.h"

#include "envoy/common/exception.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace ExactPathRewrite {

namespace {

bool isWildcardDomain(absl::string_view domain) {
  const size_t wildcard = domain.find('*');
  return wildcard != absl::string_view::npos;
}

absl::Status validatePath(absl::string_view path, absl::string_view field) {
  if (path.find('?') != absl::string_view::npos) {
    return absl::InvalidArgumentError(absl::StrCat(field, " must not contain '?': ", path));
  }
  return absl::OkStatus();
}

} // namespace

absl::StatusOr<std::unique_ptr<ExactPathRewrite>>
ExactPathRewrite::create(const ProtoExactPathRewrite& config) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = std::unique_ptr<ExactPathRewrite>(new ExactPathRewrite(config, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

ExactPathRewrite::ExactPathRewrite(const ProtoExactPathRewrite& config,
                                   absl::Status& creation_status)
    : host_header_(config.host_header()) {
  absl::flat_hash_set<std::string> domains;
  for (const auto& host_rules : config.hosts()) {
    for (const auto& domain : host_rules.domains()) {
      if (!domains.insert(domain).second) {
        creation_status = absl::InvalidArgumentError(absl::StrCat("Duplicate domain: ", domain));
        return;
      }
    }
    creation_status = addHostRules(host_rules);
    if (!creation_status.ok()) {
      return;
    }
  }
}

absl::Status ExactPathRewrite::addHostRules(const ProtoHostRules& host_rules) {
  PathRewrites path_rewrites;
  for (const auto& rule : host_rules.rules()) {
    RETURN_IF_NOT_OK(validatePath(rule.exact_path(), "exact_path"));
    RETURN_IF_NOT_OK(validatePath(rule.replacement_path(), "replacement_path"));
    if (!path_rewrites.emplace(rule.exact_path(), rule.replacement_path()).second) {
      return absl::InvalidArgumentError(absl::StrCat("Duplicate exact_path: ", rule.exact_path()));
    }
  }

  for (const auto& configured_domain : host_rules.domains()) {
    const absl::string_view domain = configured_domain;
    const size_t wildcard = domain.find('*');
    if (!isWildcardDomain(domain)) {
      exact_host_rules_.emplace(configured_domain, path_rewrites);
    } else if (domain == "*") {
      if (default_host_rules_.has_value()) {
        return absl::InvalidArgumentError("Only one '*' domain is permitted");
      }
      default_host_rules_ = path_rewrites;
    } else if (wildcard == 0 && domain.find('*', 1) == absl::string_view::npos) {
      suffix_host_rules_[domain.size() - 1].emplace(std::string(domain.substr(1)), path_rewrites);
    } else if (wildcard == domain.size() - 1 && domain.find('*') == wildcard) {
      prefix_host_rules_[domain.size() - 1].emplace(std::string(domain.substr(0, wildcard)),
                                                    path_rewrites);
    } else {
      return absl::InvalidArgumentError(absl::StrCat("Invalid wildcard domain: ", domain));
    }
  }
  return absl::OkStatus();
}

const ExactPathRewrite::PathRewrites*
ExactPathRewrite::findPathRewrites(absl::string_view host) const {
  const auto exact = exact_host_rules_.find(host);
  if (exact != exact_host_rules_.end()) {
    return &exact->second;
  }

  for (const auto& [length, rules] : suffix_host_rules_) {
    if (length < static_cast<int64_t>(host.size())) {
      const auto match = rules.find(host.substr(host.size() - length));
      if (match != rules.end()) {
        return &match->second;
      }
    }
  }
  for (const auto& [length, rules] : prefix_host_rules_) {
    if (length < static_cast<int64_t>(host.size())) {
      const auto match = rules.find(host.substr(0, length));
      if (match != rules.end()) {
        return &match->second;
      }
    }
  }
  return default_host_rules_ ? &*default_host_rules_ : nullptr;
}

bool ExactPathRewrite::mutate(Envoy::Http::RequestHeaderMap& headers,
                              const StreamInfo::StreamInfo&) const {
  const auto host = headers.get(host_header_);
  const absl::string_view host_value = host.empty() ? "" : host[0]->value().getStringView();
  const PathRewrites* path_rewrites = findPathRewrites(host_value);
  if (path_rewrites == nullptr) {
    return true;
  }

  const absl::string_view request_path = headers.getPathValue();
  const size_t query_start = request_path.find('?');
  const absl::string_view path = request_path.substr(0, query_start);
  const absl::string_view query =
      query_start == absl::string_view::npos ? "" : request_path.substr(query_start);
  if (path.find('%') != absl::string_view::npos) {
    return true;
  }

  const auto rewrite = path_rewrites->find(path);
  if (rewrite == path_rewrites->end()) {
    return true;
  }
  headers.setPath(absl::StrCat(rewrite->second, query));
  return true;
}

} // namespace ExactPathRewrite
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
