#pragma once

#include "source/common/memory/heap_shrinker.h"
#include "source/server/server_base.h"

namespace Envoy {
namespace Server {

class InstanceImpl : public InstanceBase {
public:
  using InstanceBase::InstanceBase;

protected:
  void maybeCreateHeapShrinker() override;

private:
  std::unique_ptr<Memory::HeapShrinker> heap_shrinker_;
};

} // namespace Server
} // namespace Envoy
