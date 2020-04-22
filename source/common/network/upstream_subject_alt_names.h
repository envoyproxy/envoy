#pragma once

#include "envoy/stream_info/filter_state.h"

namespace Envoy {
namespace Network {

/**
 * SANs to validate certificate to set in the upstream connection. Filters can use this one to
 * override the SAN in TLS context.
 */
class UpstreamSubjectAltNames : public StreamInfo::FilterState::Object {
public:
  explicit UpstreamSubjectAltNames(const std::vector<std::string>& upstream_subject_alt_names)
      : upstream_subject_alt_names_(upstream_subject_alt_names) {}
  const std::vector<std::string>& value() const { return upstream_subject_alt_names_; }
  static const std::string& key();

private:
  const std::vector<std::string> upstream_subject_alt_names_;
};

} // namespace Network
} // namespace Envoy
