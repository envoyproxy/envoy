#pragma once

#include <string>

namespace Lyft {
namespace Tracing {

struct TransportContext {
  std::string request_id_;
  std::string span_context_;
};

static const TransportContext EMPTY_CONTEXT = {"", ""};

} // Tracing
} // Lyft