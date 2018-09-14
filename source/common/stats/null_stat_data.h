#pragma once

#include <string>

#include "common/stats/stat_data_allocator_impl.h"

namespace Envoy {
namespace Stats {

struct NullStatData {
  explicit NullStatData() {}

  absl::string_view key() const { return name_; }
  std::string name() const { return name_; }

  std::atomic<uint64_t> value_{0};
  std::atomic<uint64_t> pending_increment_{0};
  std::atomic<uint16_t> flags_{0};
  std::atomic<uint16_t> ref_count_{1};
  std::string name_ = "null";
};

class NullStatDataAllocator : public StatDataAllocatorImpl<NullStatData> {
public:
  NullStatDataAllocator() : null_stat_ptr_(std::make_unique<NullStatData>()) {}
  ~NullStatDataAllocator() {}

  // StatDataAllocatorImpl
  NullStatData* alloc(absl::string_view name) override;
  void free(NullStatData& data) override;

  // StatDataAllocator
  bool requiresBoundedStatNameSize() const override { return false; }

private:
  std::unique_ptr<NullStatData> null_stat_ptr_;
};

} // namespace Stats
} // namespace Envoy
