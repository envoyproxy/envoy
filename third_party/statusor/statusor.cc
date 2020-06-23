/**
 * IMPORTANT: this file is a fork of the soon to be open-source absl::StatusOr class.
 * When the absl::StatusOr lands this file will be removed.
 */

/*
 * Copyright 2019 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "third_party/statusor/statusor.h"

#include <ostream>

#include "common/common/assert.h"

namespace absl {

namespace internal_statusor {

void Helper::HandleInvalidStatusCtorArg(absl::Status* status) {
  const char* kMessage = "An OK status is not a valid constructor argument to StatusOr<T>";
  ASSERT(false, kMessage);
  // In optimized builds, we will fall back to ::util::error::INTERNAL.
  *status = absl::Status(absl::StatusCode::kInternal, kMessage);
}

void Helper::Crash(const absl::Status&) { abort(); }

} // namespace internal_statusor

} // namespace Envoy
