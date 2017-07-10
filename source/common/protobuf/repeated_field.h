#pragma once

#include "google/protobuf/repeated_field.h"

namespace Envoy {
namespace Protobuf {

template <class T> using RepeatedPtrField = google::protobuf::RepeatedPtrField<T>;

} // namespace Protobuf
} // namespace Envoy
