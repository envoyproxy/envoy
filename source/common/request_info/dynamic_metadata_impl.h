#pragma once

#include <map>

#include "envoy/request_info/dynamic_metadata.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace RequestInfo {

class DynamicMetadataImpl : public DynamicMetadata {
public:
  DynamicMetadataImpl() {}
  ~DynamicMetadataImpl() override {};

  // DynamicMetadata
  void setData(absl::string_view data_name,
               std::unique_ptr<DynamicMetadataObject>&& data) override;
  const DynamicMetadataObject* getDataGeneric(absl::string_view data_name) const override;

private:
  std::map<std::string, std::unique_ptr<DynamicMetadataObject>, std::less<>> data_storage_;
};

} // namespace RequestInfo
} // namespace Envoy
