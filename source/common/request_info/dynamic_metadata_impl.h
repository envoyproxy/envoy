#pragma once

#include <map>

#include "envoy/request_info/dynamic_metadata.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace RequestInfo {

class DynamicMetadataImpl : public DynamicMetadata {
public:
  // DynamicMetadata
  void setData(absl::string_view data_name, std::unique_ptr<Object>&& data) override;
  bool hasDataWithName(absl::string_view) const override;
  const Object* getDataGeneric(absl::string_view data_name) const override;

private:
  std::map<std::string, std::unique_ptr<Object>, std::less<>> data_storage_;
};

} // namespace RequestInfo
} // namespace Envoy
