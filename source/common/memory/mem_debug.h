#pragma once

namespace Envoy {

// Called to force-load the memory debugging module, which (when tcmalloc is
// disabled) overrides operator new/delete. See comments in the .cc file for
// more details.
void MemDebugLoader();

} // namespace Envoy
