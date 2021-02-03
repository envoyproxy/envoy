#include <string.h>

namespace Envoy {

char* get_error_illegal(int err) { return strerror(err); }
char* get_error_legal1(int err) { return some_other_strerror(err); }
char* get_error_legal2(int err) { return strerror2(err); }

} // namespace Envoy
