#include "source/extensions/common/dynamic_forward_proxy/dynamic_host_candidates.h"

#include "envoy/registry/registry.h"

#include "source/common/common/macros.h"
#include "source/common/http/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

namespace {

// A DNS name, an IPv4 address or a bracketed IPv6 address, with no port.
bool validHost(absl::string_view host) {
  const auto authority = Http::Utility::parseAuthority(host);
  if (authority.host_.empty() || authority.port_.has_value()) {
    return false;
  }
  return !absl::StrContains(host, ':') || (authority.is_ip_address_ && host.front() == '[');
}

class DynamicHostCandidatesObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return DynamicHostCandidates::key(); }
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    return DynamicHostCandidates::fromString(data);
  }
};

REGISTER_FACTORY(DynamicHostCandidatesObjectFactory, StreamInfo::FilterState::ObjectFactory);

} // namespace

const std::string& DynamicHostCandidates::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.upstream.dynamic_host_candidates");
}

DynamicHostCandidates::DynamicHostCandidates(std::vector<Candidate> candidates)
    : candidates_(std::move(candidates)) {}

std::unique_ptr<DynamicHostCandidates> DynamicHostCandidates::fromString(absl::string_view value) {
  std::vector<Candidate> candidates;
  for (absl::string_view entry : absl::StrSplit(value, ',')) {
    entry = absl::StripAsciiWhitespace(entry);
    if (entry.empty()) {
      return nullptr;
    }
    const auto authority = Http::Utility::parseAuthority(entry);
    if (authority.port_.has_value() && authority.port_.value() == 0) {
      return nullptr;
    }
    // parseAuthority() strips IPv6 brackets, which the DFP host key keeps.
    std::string host = authority.is_ip_address_ && absl::StrContains(authority.host_, ':')
                           ? absl::StrCat("[", authority.host_, "]")
                           : std::string(authority.host_);
    if (!validHost(host) || (host.front() == '[' && entry.front() != '[')) {
      return nullptr;
    }
    candidates.push_back({std::move(host), authority.port_.value_or(0)});
  }
  return std::make_unique<DynamicHostCandidates>(std::move(candidates));
}

std::optional<std::string> DynamicHostCandidates::serializeAsString() const {
  return absl::StrJoin(candidates_, ",", [](std::string* out, const Candidate& candidate) {
    absl::StrAppend(out, candidate.host);
    if (candidate.port != 0) {
      absl::StrAppend(out, ":", candidate.port);
    }
  });
}

} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
