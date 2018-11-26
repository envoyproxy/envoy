#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/api/v2/route/route.pb.h"

#include "common/http/header_utility.h"

#include "extensions/filters/network/thrift_proxy/metadata.h"
#include "extensions/filters/network/thrift_proxy/router/router.h"
#include "extensions/filters/network/thrift_proxy/router/router_ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

/**
 * Action for source cluster rate limiting.
 */
class SourceClusterAction : public RateLimitAction {
public:
  // Router::RateLimitAction
  bool populateDescriptor(const Router::RouteEntry& route, RateLimit::Descriptor& descriptor,
                          const std::string& local_service_cluster, const MessageMetadata& metadata,
                          const Network::Address::Instance& remote_address) const override;
};

/**
 * Action for destination cluster rate limiting.
 */
class DestinationClusterAction : public RateLimitAction {
public:
  // Router::RateLimitAction
  bool populateDescriptor(const Router::RouteEntry& route, RateLimit::Descriptor& descriptor,
                          const std::string& local_service_cluster, const MessageMetadata& metadata,
                          const Network::Address::Instance& remote_address) const override;
};

/**
 * Action for request headers rate limiting.
 */
class RequestHeadersAction : public RateLimitAction {
public:
  RequestHeadersAction(const envoy::api::v2::route::RateLimit::Action::RequestHeaders& action)
      : header_name_(action.header_name()), descriptor_key_(action.descriptor_key()),
        use_method_name_(header_name_ == Headers::get().MethodName) {}

  // Router::RateLimitAction
  bool populateDescriptor(const Router::RouteEntry& route, RateLimit::Descriptor& descriptor,
                          const std::string& local_service_cluster, const MessageMetadata& metadata,
                          const Network::Address::Instance& remote_address) const override;

private:
  const Http::LowerCaseString header_name_;
  const std::string descriptor_key_;
  const bool use_method_name_;
};

/**
 * Action for remote address rate limiting.
 */
class RemoteAddressAction : public RateLimitAction {
public:
  // Router::RateLimitAction
  bool populateDescriptor(const Router::RouteEntry& route, RateLimit::Descriptor& descriptor,
                          const std::string& local_service_cluster, const MessageMetadata& metadata,
                          const Network::Address::Instance& remote_address) const override;
};

/**
 * Action for generic key rate limiting.
 */
class GenericKeyAction : public RateLimitAction {
public:
  GenericKeyAction(const envoy::api::v2::route::RateLimit::Action::GenericKey& action)
      : descriptor_value_(action.descriptor_value()) {}

  // Router::RateLimitAction
  bool populateDescriptor(const Router::RouteEntry& route, RateLimit::Descriptor& descriptor,
                          const std::string& local_service_cluster, const MessageMetadata& metadata,
                          const Network::Address::Instance& remote_address) const override;

private:
  const std::string descriptor_value_;
};

/**
 * Action for header value match rate limiting.
 */
class HeaderValueMatchAction : public RateLimitAction {
public:
  HeaderValueMatchAction(const envoy::api::v2::route::RateLimit::Action::HeaderValueMatch& action);

  // Router::RateLimitAction
  bool populateDescriptor(const Router::RouteEntry& route, RateLimit::Descriptor& descriptor,
                          const std::string& local_service_cluster, const MessageMetadata& metadata,
                          const Network::Address::Instance& remote_address) const override;

private:
  const std::string descriptor_value_;
  const bool expect_match_;
  std::vector<Http::HeaderUtility::HeaderData> action_headers_;
};

/*
 * Implementation of RateLimitPolicyEntry that holds the action for the configuration.
 */
class RateLimitPolicyEntryImpl : public RateLimitPolicyEntry {
public:
  RateLimitPolicyEntryImpl(const envoy::api::v2::route::RateLimit& config);

  // Router::RateLimitPolicyEntry
  uint32_t stage() const override { return stage_; }
  const std::string& disableKey() const override { return disable_key_; }
  void populateDescriptors(const Router::RouteEntry& route,
                           std::vector<Envoy::RateLimit::Descriptor>& descriptors,
                           const std::string& local_service_cluster,
                           const MessageMetadata& metadata,
                           const Network::Address::Instance& remote_address) const override;

private:
  const std::string disable_key_;
  uint32_t stage_;
  std::vector<RateLimitActionPtr> actions_;
};

/**
 * Implementation of RateLimitPolicy that reads from the JSON route config.
 */
class RateLimitPolicyImpl : public RateLimitPolicy {
public:
  RateLimitPolicyImpl(
      const Protobuf::RepeatedPtrField<envoy::api::v2::route::RateLimit>& rate_limits);

  // Router::RateLimitPolicy
  const std::vector<std::reference_wrapper<const RateLimitPolicyEntry>>&
  getApplicableRateLimit(uint32_t stage = 0) const override;
  bool empty() const override { return rate_limit_entries_.empty(); }

  static constexpr uint32_t MAX_STAGE_NUMBER = 10;

private:
  std::vector<std::unique_ptr<RateLimitPolicyEntry>> rate_limit_entries_;
  std::vector<std::vector<std::reference_wrapper<const RateLimitPolicyEntry>>>
      rate_limit_entries_reference_;
};

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
