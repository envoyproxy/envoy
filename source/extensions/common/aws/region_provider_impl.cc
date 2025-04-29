#include "source/extensions/common/aws/region_provider_impl.h"

#include "source/extensions/common/aws/utility.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

constexpr char AWS_REGION[] = "AWS_REGION";
constexpr char AWS_DEFAULT_REGION[] = "AWS_DEFAULT_REGION";
constexpr char REGION[] = "REGION";
constexpr char AWS_SIGV4A_SIGNING_REGION_SET[] = "AWS_SIGV4A_SIGNING_REGION_SET";
constexpr char SIGV4A_SIGNING_REGION_SET[] = "SIGV4A_SIGNING_REGION_SET";

absl::optional<std::string> EnvironmentRegionProvider::getRegion() {
  std::string region;

  // Search for the region in environment variables AWS_REGION and AWS_DEFAULT_REGION
  region = Utility::getEnvironmentVariableOrDefault(AWS_REGION, "");
  if (region.empty()) {
    region = Utility::getEnvironmentVariableOrDefault(AWS_DEFAULT_REGION, "");
    if (region.empty()) {
      return absl::nullopt;
    }
  }
  ENVOY_LOG(debug, "EnvironmentRegionProvider: Region string retrieved: {}", region);
  return region;
}

absl::optional<std::string> EnvironmentRegionProvider::getRegionSet() {
  std::string regionSet;

  // Search for the region in environment variables AWS_REGION and AWS_DEFAULT_REGION
  regionSet = Utility::getEnvironmentVariableOrDefault(AWS_SIGV4A_SIGNING_REGION_SET, "");
  if (regionSet.empty()) {
    return absl::nullopt;
  }
  ENVOY_LOG(debug, "EnvironmentRegionProvider: RegionSet string retrieved: {}", regionSet);
  return regionSet;
}

AWSCredentialsFileRegionProvider::AWSCredentialsFileRegionProvider(
    const envoy::extensions::common::aws::v3::CredentialsFileCredentialProvider&
        credential_file_config) {
  if (credential_file_config.has_credentials_data_source() &&
      credential_file_config.credentials_data_source().has_filename()) {
    credential_file_path_ = credential_file_config.credentials_data_source().filename();
  }
  if (!credential_file_config.profile().empty()) {
    profile_ = credential_file_config.profile();
  }
}

absl::optional<std::string> AWSCredentialsFileRegionProvider::getRegion() {
  absl::flat_hash_map<std::string, std::string> elements = {{REGION, ""}};
  absl::flat_hash_map<std::string, std::string>::iterator it;

  std::string file_path, profile;
  file_path = credential_file_path_.has_value() ? credential_file_path_.value()
                                                : Utility::getCredentialFilePath();
  profile = profile_.has_value() ? profile_.value() : Utility::getCredentialProfileName();

  // Search for the region in the credentials file
  if (!Utility::resolveProfileElementsFromFile(file_path, profile, elements)) {
    return absl::nullopt;
  }
  it = elements.find(REGION);
  if (it == elements.end() || it->second.empty()) {
    return absl::nullopt;
  }

  ENVOY_LOG(debug, "AWSCredentialsFileRegionProvider: Region string retrieved: {}", it->second);
  return it->second;
}

absl::optional<std::string> AWSCredentialsFileRegionProvider::getRegionSet() {
  absl::flat_hash_map<std::string, std::string> elements = {{SIGV4A_SIGNING_REGION_SET, ""}};
  absl::flat_hash_map<std::string, std::string>::iterator it;

  std::string file_path, profile;
  file_path = credential_file_path_.has_value() ? credential_file_path_.value()
                                                : Utility::getCredentialFilePath();

  profile = profile_.has_value() ? profile_.value() : Utility::getCredentialProfileName();

  // Search for the region in the credentials file
  if (!Utility::resolveProfileElementsFromFile(file_path, profile, elements)) {
    return absl::nullopt;
  }
  it = elements.find(SIGV4A_SIGNING_REGION_SET);
  if (it == elements.end() || it->second.empty()) {
    return absl::nullopt;
  }

  ENVOY_LOG(debug, "AWSCredentialsFileRegionProvider: RegionSet string retrieved: {}", it->second);
  return it->second;
}

absl::optional<std::string> AWSConfigFileRegionProvider::getRegion() {
  absl::flat_hash_map<std::string, std::string> elements = {{REGION, ""}};
  absl::flat_hash_map<std::string, std::string>::iterator it;

  // Search for the region in the config file

  if (!Utility::resolveProfileElementsFromFile(Utility::getConfigFilePath(),
                                               Utility::getConfigProfileName(), elements)) {
    return absl::nullopt;
  }

  it = elements.find(REGION);
  if (it == elements.end() || it->second.empty()) {
    return absl::nullopt;
  }

  ENVOY_LOG(debug, "AWSConfigFileRegionProvider: Region string retrieved: {}", it->second);
  return it->second;
}

absl::optional<std::string> AWSConfigFileRegionProvider::getRegionSet() {
  absl::flat_hash_map<std::string, std::string> elements = {{SIGV4A_SIGNING_REGION_SET, ""}};
  absl::flat_hash_map<std::string, std::string>::iterator it;

  // Search for the region in the config file

  if (!Utility::resolveProfileElementsFromFile(Utility::getConfigFilePath(),
                                               Utility::getConfigProfileName(), elements)) {
    return absl::nullopt;
  }

  it = elements.find(SIGV4A_SIGNING_REGION_SET);
  if (it == elements.end() || it->second.empty()) {
    return absl::nullopt;
  }

  ENVOY_LOG(debug, "AWSConfigFileRegionProvider: RegionSet string retrieved: {}", it->second);
  return it->second;
}

// Region provider chain. This allows retrieving region information from the following locations (in
// order):
// 1. The envoy configuration, in the region parameter
// 2. The envoy environment, in AWS_REGION then AWS_DEFAULT_REGION
// 3. In the credentials file $HOME/.aws/credentials (or location from
//    AWS_SHARED_CREDENTIALS_FILE/AWS_DEFAULT_SHARED_CREDENTIALS_FILE), under profile section
//    specified by AWS_PROFILE
// 4. In the config file $HOME/.aws/config (or location from AWS_CONFIG_FILE), under profile section
// specified by AWS_PROFILE
//
// Credentials and profile format can be found here:
// https://docs.aws.amazon.com/cli/latest/userguide/cli-configure-files.html
//
RegionProviderChain::RegionProviderChain(
    const envoy::extensions::common::aws::v3::CredentialsFileCredentialProvider&
        credential_file_config) {
  // TODO(nbaws): Verify that bypassing virtual dispatch here was intentional
  add(RegionProviderChain::createEnvironmentRegionProvider());
  add(RegionProviderChain::createAWSCredentialsFileRegionProvider(credential_file_config));
  add(RegionProviderChain::createAWSConfigFileRegionProvider());
}

absl::optional<std::string> RegionProviderChain::getRegion() {
  for (auto& provider : providers_) {
    const auto region = provider->getRegion();
    if (region.has_value()) {
      return region;
    }
  }
  return absl::nullopt;
}

absl::optional<std::string> RegionProviderChain::getRegionSet() {
  for (auto& provider : providers_) {
    const auto regionSet = provider->getRegionSet();
    if (regionSet.has_value()) {
      return regionSet;
    }
  }
  return absl::nullopt;
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
