#pragma once

#include "envoy/config/xds_resources_delegate.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Config {

class XdsConfigSourceId : public XdsSourceId {
public:
  XdsConfigSourceId(absl::string_view authority_id, absl::string_view resource_type_url);

  std::string toKey() const override;

private:
  const std::string authority_id_;
  const std::string resource_type_url_;
};

} // namespace Config
} // namespace Envoy
