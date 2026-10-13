#pragma once

#include "envoy/http/codes.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FileServer {

Http::Code abslStatusToHttpStatus(absl::StatusCode code);

} // namespace FileServer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
