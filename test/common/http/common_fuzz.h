#include <functional>

#include "absl/strings/string_view.h"

namespace Envoy {

// The HeaderMap code assumes that input does not contain certain characters, and
// this is validated by the HTTP parser. Some fuzzers will create strings with
// these characters, however, and this creates not very interesting fuzz test
// failures as an assertion is rapidly hit in the LowerCaseString constructor
// before we get to anything interesting.
//
// This method will replace any of those characters found with spaces.
std::string replaceInvalidCharacters(absl::string_view string);

} // namespace Envoy
