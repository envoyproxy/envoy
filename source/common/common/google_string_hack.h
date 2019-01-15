#pragma once

#include <string>

namespace Envoy {

// This is a hack for Google's import process. Inside Google, many protobuf and
// absl APIs work on 'string' rather than 'std::string', and they are different
// for historical reasons. Using this type nickname helps us identify places
// where Google needs to modify string types via sed process during import.
//
// This hack will hopefully not last forever.
using GoogleStringHack = std::string;

} // namespace Envoy
