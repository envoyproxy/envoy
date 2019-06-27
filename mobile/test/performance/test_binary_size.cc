#include "main_interface.h"

// NOLINT(namespace-envoy)

// This binary is used to perform stripped down binary size investigations of the Envoy codebase.
// Please refer to the development docs for more information:
// https://envoy-mobile.github.io/docs/envoy-mobile/latest/development/performance/binary_size.html
int main() { return run_envoy(nullptr); }
