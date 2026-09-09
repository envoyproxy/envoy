#pragma once

#include <map>
#include <optional>
#include <string>

#include "envoy/extensions/http/early_header_mutation/exact_path_rewrite/v3/exact_path_rewrite.pb.h"
#include "envoy/http/early_header_mutation.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace ExactPathRewrite {

using ProtoExactPathRewrite =
    envoy::extensions::http::early_header_mutation::exact_path_rewrite::v3::ExactPathRewrite;
using ProtoHostRules =
    envoy::extensions::http::early_header_mutation::exact_path_rewrite::v3::HostRules;

class ExactPathRewrite : public Envoy::Http::EarlyHeaderMutation {
public:
  static absl::StatusOr<std::unique_ptr<ExactPathRewrite>>
  create(const ProtoExactPathRewrite& config);

  bool mutate(Envoy::Http::RequestHeaderMap& headers,
              const StreamInfo::StreamInfo& stream_info) const override;

private:
  using PathRewrites = absl::flat_hash_map<std::string, std::string>;
  using HostRulesMap = absl::flat_hash_map<std::string, PathRewrites>;
  using WildcardHostRules = std::map<int64_t, HostRulesMap, std::greater<>>;

  ExactPathRewrite(const ProtoExactPathRewrite& config, absl::Status& creation_status);

  const PathRewrites* findPathRewrites(absl::string_view host) const;
  absl::Status addHostRules(const ProtoHostRules& host_rules);

  Envoy::Http::LowerCaseString host_header_;
  HostRulesMap exact_host_rules_;
  WildcardHostRules suffix_host_rules_;
  WildcardHostRules prefix_host_rules_;
  std::optional<PathRewrites> default_host_rules_;
};

} // namespace ExactPathRewrite
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
