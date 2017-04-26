#!/bin/bash

[[ -z "${GIT_BUILD_SHA}" ]] && GIT_BUILD_SHA="0000000000000000000000000000000000000000"

cat <<EOF > version_generated.cc
#include "common/common/version.h"

const std::string VersionInfo::GIT_SHA("${GIT_BUILD_SHA}");
EOF

cat <<EOF > BUILD
exports_files(["version_generated.cc"])
EOF
