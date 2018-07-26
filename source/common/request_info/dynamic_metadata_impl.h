#pragma once

#include <map>

#include "envoy/request_info/dynamic_metadata.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace RequestInfo {

class DynamicMetadataImpl : public DynamicMetadata {
public:
  DynamicMetadataImpl() {}
  ~DynamicMetadataImpl() override;

  // DynamicMetadata
  void setDataGeneric(absl::string_view data_name, size_t type_id, void* data,
                      void (*destructor)(void*)) override;
  void* getDataGeneric(absl::string_view data_name, size_t type_id) const override;
  bool hasDataGeneric(absl::string_view data_name, size_t type_id) const override;
  bool hasDataWithName(absl::string_view data_name) const override;

private:
  struct Data {
    size_t typeid_;
    void* ptr_;
    void (*destructor_)(void*);
  };

  std::map<std::string, Data, std::less<>> data_storage_;
};

} // namespace RequestInfo
} // namespace Envoy
