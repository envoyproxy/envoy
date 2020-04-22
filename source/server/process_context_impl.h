#pragma once

#include "envoy/server/process_context.h"

namespace Envoy {

class ProcessContextImpl : public ProcessContext {
public:
  ProcessContextImpl(ProcessObject& process_object) : process_object_(process_object) {}
  // ProcessContext
  ProcessObject& get() const override { return process_object_; }

private:
  ProcessObject& process_object_;
};

} // namespace Envoy
