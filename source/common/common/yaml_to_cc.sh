#!/bin/bash

# Do not generate test suites for empty source files.
if [ -z "$1" ]; then
    exit 0
fi

# Create .cc file with const std::string containing contents of the .yaml manifest

(
  cat << EOF

#include <string>

namespace Envoy {

extern const std::string manifest_yaml = R"(
EOF
)

cat "$1"

(
  cat << EOF

)";

}
EOF
)
