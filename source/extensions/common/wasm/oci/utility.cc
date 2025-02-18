#include "source/extensions/common/wasm/oci/utility.h"

#include <string>

#include "source/common/json/json_loader.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace Oci {

absl::Status parseImageURI(const std::string& uri, std::string& registry, std::string& image_name,
                           std::string& tag) {
  const std::string prefix = "oci://";
  if (!absl::StartsWith(uri, prefix)) {
    return {absl::StatusCode::kInvalidArgument, "Not OCI image URI"};
  }

  auto without_prefix = uri.substr(prefix.length());
  auto slash_pos = without_prefix.find('/');
  if (slash_pos == std::string::npos) {
    return {absl::StatusCode::kInvalidArgument, "URI does not include '/'"};
  }

  registry = without_prefix.substr(0, slash_pos);
  size_t colon_pos = without_prefix.find_last_of(':');
  if (colon_pos == std::string::npos || colon_pos < slash_pos + 1) {
    return {absl::StatusCode::kInvalidArgument, "URI does not include ':'"};
  }

  image_name = without_prefix.substr(slash_pos + 1, colon_pos - (slash_pos + 1));
  tag = without_prefix.substr(colon_pos + 1);

  return absl::OkStatus();
}

absl::StatusOr<std::string> prepareAuthorizationHeader(std::string image_pull_secret_raw,
                                                       std::string registry) {
  if (image_pull_secret_raw.empty()) {
    return absl::InvalidArgumentError("Empty image pull secret");
  }

  Json::ObjectSharedPtr image_pull_secret;
  try {
    Json::ObjectSharedPtr image_pull_secret = THROW_OR_RETURN_VALUE(
        Json::Factory::loadFromString(image_pull_secret_raw), Json::ObjectSharedPtr);
  } catch (EnvoyException& e) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to parse image pull secret: ", e.what()));
  }

  Json::ObjectSharedPtr auths;
  try {
    auths = THROW_OR_RETURN_VALUE(image_pull_secret->getObject("auths"), Json::ObjectSharedPtr);
  } catch (EnvoyException& e) {
    return absl::InvalidArgumentError(
        absl::StrCat("Did not find 'auths' key in the image pull secret: ", e.what()));
  }

  Json::ObjectSharedPtr registry_object;
  try {
    registry_object = THROW_OR_RETURN_VALUE(auths->getObject(registry), Json::ObjectSharedPtr);
  } catch (EnvoyException& e) {
    return absl::InvalidArgumentError(absl::StrCat("Did not find 'auths.", registry,
                                                   "' key in the image pull secret: ", e.what()));
  }

  auto auth = registry_object->getString("auth");
  if (!auth.ok()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Did not find 'auths.", registry, ".auth' key in the image pull secret"));
  }

  // TODO(jewertow): handle other registries
  if (!absl::StrContains(registry, ".dkr.ecr.") || !absl::EndsWith(registry, ".amazonaws.com")) {
    return absl::InvalidArgumentError("Unsupported registry - currently, only ECR is supported");
  }

  // ECR uses basic auth and the "auth" key in the image pull secret should contain base64-encoded
  // AWS:<password> so it ca be passed as is to the request.
  return absl::StrCat("Basic ", auth->c_str());
}

} // namespace Oci
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
