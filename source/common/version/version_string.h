#pragma once

#include <string>

namespace Envoy {

// Returns the Envoy version string (e.g. "1.32.0/abc123.Modified/RELEASE/BoringSSL").
// This is intentionally a slim alternative to VersionInfo::version() for use by low-level
// libraries (such as the logger) that cannot depend on version_lib without creating a
// circular BUILD dependency through protobuf.
const std::string& envoyVersionString();

// Returns the build type string: "RELEASE" or "DEBUG".
const std::string& envoyBuildType();

// Returns the SSL library version string (e.g. "BoringSSL", "OpenSSL").
const std::string& envoySSLVersion();

} // namespace Envoy
