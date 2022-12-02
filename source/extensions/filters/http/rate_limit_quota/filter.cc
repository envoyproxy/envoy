#include "source/extensions/filters/http/rate_limit_quota/filter.h"

#include <memory>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using ::envoy::type::v3::RateLimitStrategy;
using RateLimitQuotaBucketSettings =
    ::envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaBucketSettings;

void RateLimitQuotaFilter::setDecoderFilterCallbacks(
    Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

// TODO(tyxia) Mostly are example/boilerplate code, polish implementation here.
// Http::FilterHeadersStatus RateLimitQuotaFilter::decodeHeaders(Http::RequestHeaderMap& headers,
//                                                               bool) {
//   // Start the stream on the first request.
//   auto start_stream = rate_limit_client_->startStream(callbacks_->streamInfo());
//   if (!start_stream.ok()) {
//     // TODO(tyxia) Consider adding the log.
//     return Envoy::Http::FilterHeadersStatus::Continue;
//   }
//   absl::StatusOr<BucketId> match_result = requestMatching(headers);

//   // Request is not matched by any matchers. In this case, requests are ALLOWED by default (i.e.,
//   // fail-open) and will not be reported to RLQS server.
//   if (!match_result.ok()) {
//     return Envoy::Http::FilterHeadersStatus::Continue;
//   }

//   BucketId bucket_id = match_result.value();
//   // Request is not matched by any matcher but the `on_no_match` field is configured. In this
//   // case, the request is matched to catch-all bucket and is DENIED by default.
//   // TODO(tyxia) Think about the way of representing DENIED and ALLOWED here.
//   if (bucket_id.bucket().empty()) {
//     return Envoy::Http::FilterHeadersStatus::Continue;
//   }

//   // Request has been matched and the corresponding bucket id has been generated successfully.
//   // Retrieve the quota assignment, if the entry with specific `bucket_id` is found.
//   if (quota_assignment_.find(bucket_id) != quota_assignment_.end()) {
//     // Don't need to send reports.
//   } else {
//     if (quota_bucket_ != nullptr) {
//       BucketElement element;
//       element.rate_limit_client_ = createRateLimitClient(factory_context_,
//       config_->rlqs_server()); element.send_reports_timer =
//           factory_context_.mainThreadDispatcher().createTimer([&element]() -> void {
//             ASSERT(element.rate_limit_client_ != nullptr);
//             element.rate_limit_client_->sendUsageReport(absl::nullopt);
//           });
//       (*quota_bucket_)[bucket_id] = std::move(element);
//     }
//     // Otherwise, send the request to RLQS server to get the quota assignment from RLQS when the
//     // request matches a bucket for the first time.
//     // TODO(tyxia) Here triggers the send function for the first time, later the send function
//     // will be triggered periodically.
//     // The interface of asking the quota assignment and the interface of reports the
//     // quota usage.
//     // TODO(tyxia) If the request is mapped to one bucket, i.e., one bucket_id is generated.
//     // Then we only have one usage report to sent, which also means we only have one usage report
//     to
//     // build Then why
//     //
//     https://source.corp.google.com/piper///depot/google3/third_party/envoy/src/api/envoy/service/rate_limit_quota/v3/rlqs.proto;rcl=464148816;l=95
//     // here has a list of reports to build and to report. We batch the reports and send them all
//     // together???
//     rate_limit_client_->sendUsageReport(bucket_id);
//     // While the filter is waiting for the response from the RLQS server,
//     // Wait for the quota assignment from the RLQS server.
//     return Envoy::Http::FilterHeadersStatus::StopAllIterationAndWatermark;
//     // Then insert the bucket_id to the map once
//   }

//   rate_limit_client_->rateLimit(*this);

//   return Envoy::Http::FilterHeadersStatus::Continue;
// }

// Http::FilterHeadersStatus RateLimitQuotaFilter::decodeHeaders(Http::RequestHeaderMap& headers,
//                                                               bool) {
//   // TODO(tyxia) Create the rate limit gRPC client and start the stream on the first request.
//   absl::StatusOr<BucketId> match_result = requestMatching(headers);

//   // Request is not matched by any matchers. In this case, requests are ALLOWED by default (i.e.,
//   // fail-open) and will not be reported to RLQS server.
//   if (!match_result.ok()) {
//     return Envoy::Http::FilterHeadersStatus::Continue;
//   }

//   // Request is not matched by any matcher but the `on_no_match` field is configured. In this
//   // case, the request is matched to catch-all bucket and is DENIED by default.
//   // TODO(tyxia) Think about the way of representing DENIED and ALLOWED here.
//   if (match_result.value().bucket().empty()) {
//     return Envoy::Http::FilterHeadersStatus::Continue;
//   }

//   // Request has been matched and the corresponding bucket id has been generated successfully.
//   // Retrieve the quota assignment, if the entry with specific `bucket_id` is found.
//   if (quota_assignment_.find(match_result.value()) != quota_assignment_.end()) {
//   }
//   // Otherwise, send the request to RLQS server for the quota assignment and insert the bucket_id to
//   // the map.

//   return Envoy::Http::FilterHeadersStatus::Continue;
// }

void RateLimitQuotaFilter::createMatcher() {
  RateLimitOnMactchActionContext context;
  Matcher::MatchTreeFactory<Http::HttpMatchingData, RateLimitOnMactchActionContext> factory(
      context, factory_context_.getServerFactoryContext(), visitor_);
  if (config_->has_bucket_matchers()) {
    matcher_ = factory.create(config_->bucket_matchers())();
  }
}

absl::StatusOr<BucketId>
RateLimitQuotaFilter::requestMatching(const Http::RequestHeaderMap& headers) {
  // Initialize the data pointer on first use and reuse it for subsequent requests.
  // This avoids creating the data object for every request, which is expensive.
  if (data_ptr_ == nullptr) {
    if (callbacks_ != nullptr) {
      data_ptr_ = std::make_unique<Http::Matching::HttpMatchingDataImpl>(
          callbacks_->streamInfo().downstreamAddressProvider());
    } else {
      return absl::InternalError("Filter callback has not been initialized successfully yet.");
    }
  }

  if (matcher_ == nullptr) {
    return absl::InternalError("Matcher has not been initialized yet");
  } else {
    data_ptr_->onRequestHeaders(headers);
    // TODO(tyxia) This function should trigger the CEL expression matching. Here, we need to
    // implement the custom_matcher and factory, also statically register it so that CEL matching
    // will be triggered with its own match() method.
    auto match_result = Matcher::evaluateMatch<Http::HttpMatchingData>(*matcher_, *data_ptr_);

    if (match_result.match_state_ == Matcher::MatchState::MatchComplete) {
      if (match_result.result_) {
        // on_match case.
        const auto result = match_result.result_();
        const RateLimitOnMactchAction* match_action =
            dynamic_cast<RateLimitOnMactchAction*>(result.get());
        const int64_t reporting_interval =
            PROTOBUF_GET_MS_REQUIRED(match_action->bucketSettings(), reporting_interval);
        // Set the reports send timer which will be triggered at reporting interval.
        send_reports_timer_->enableTimer(std::chrono::milliseconds(reporting_interval));
        // Try to generate the bucket id if the matching succeeded.
        return match_action->generateBucketId(*data_ptr_, factory_context_, visitor_);
      } else {
        return absl::NotFoundError("The match was completed, no match found");
      }
    } else {
      // The returned state from `evaluateMatch` function is `MatchState::UnableToMatch` here.
      return absl::InternalError("Unable to match the request");
    }
  }
}

absl::StatusOr<BucketId>
RateLimitOnMactchAction::generateBucketId(const Http::Matching::HttpMatchingDataImpl& data,
                                          Server::Configuration::FactoryContext& factory_context,
                                          RateLimitQuotaValidationVisitor& visitor) const {
  BucketId bucket_id;
  std::unique_ptr<Matcher::MatchInputFactory<Http::HttpMatchingData>> input_factory_ptr = nullptr;

  // Generate the `BucketId` based on the bucked id builder from the configuration.
  for (const auto& id_builder : setting_.bucket_id_builder().bucket_id_builder()) {
    std::string bucket_id_key = id_builder.first;
    auto builder_method = id_builder.second;

    // Generate the bucket id based on builder method type.
    switch (builder_method.value_specifier_case()) {
    // Retrieve the string value directly from the config (static method).
    case ValueSpecifierCase::kStringValue: {
      bucket_id.mutable_bucket()->insert({bucket_id_key, builder_method.string_value()});
      break;
    }
    // Retrieve the value from the `custom_value` typed extension config (dynamic method).
    case ValueSpecifierCase::kCustomValue: {
      // Initialize the pointer to input factory on first use.
      if (input_factory_ptr == nullptr) {
        input_factory_ptr = std::make_unique<Matcher::MatchInputFactory<Http::HttpMatchingData>>(
            factory_context.messageValidationVisitor(), visitor);
      }
      // Create `DataInput` factory callback from the config.
      Matcher::DataInputFactoryCb<Http::HttpMatchingData> data_input_cb =
          input_factory_ptr->createDataInput(builder_method.custom_value());
      auto result = data_input_cb()->get(data);
      // If result has data.
      if (result.data_) {
        if (!result.data_.value().empty()) {
          // Build the bucket id from the matched result.
          bucket_id.mutable_bucket()->insert({bucket_id_key, result.data_.value()});
        } else {
          return absl::InternalError("Empty matched result from custom value config.");
        }
      } else {
        // Currently, this line will be hitted if we configure the `custom_value` in
        // `on_no_match` action.
        return absl::InternalError("No matched result from custom value config.");
      }
      break;
    }
    case ValueSpecifierCase::VALUE_SPECIFIER_NOT_SET: {
      break;
    }
      PANIC_DUE_TO_CORRUPT_ENUM;
    }
  }

  /* TODO(tyxia) Remove the noAssignment behavior and expiredAssignment behavior from
     generateBucketId because those two should only be used for when there is no response from RLQS
     server.
  */
  // Add the no assignment behavior if the corresponding config is present.
  // TODO(tyxia) Include the additional info and refactor the return object along the callstack,
  // to indicate the it is on_no_match and handle this blanket rule in the caller site.
  if (setting_.has_no_assignment_behavior()) {
    // Retrive the value from the `blanket_rule` in the config to decide if we want to fail-open or
    // fail-close.
    auto strategy = setting_.no_assignment_behavior().fallback_rate_limit();
    if (strategy.blanket_rule() == RateLimitStrategy::ALLOW_ALL) {
    }
  }

  // Add the expired assignment behavior if the corresponding config is present.
  if (setting_.has_expired_assignment_behavior()) {
    // Retrive the value from the `blanket_rule` in the config to decide if we want to fail-open or
    // fail-close.
    auto strategy = setting_.expired_assignment_behavior().fallback_rate_limit();
    switch (strategy.blanket_rule()) {
    case RateLimitStrategy::ALLOW_ALL: {
    }
    case RateLimitStrategy::DENY_ALL: {
    }
    default: {
    }
    }
    // if (strategy.blanket_rule() == RateLimitStrategy::ALLOW_ALL) {
    // }
  }
  return bucket_id;
}

/**
 * Static registration for the on match action factory.
 */
REGISTER_FACTORY(RateLimitOnMactchActionFactory,
                 Matcher::ActionFactory<RateLimitOnMactchActionContext>);

/************************************************************************************************


updated version using `requestMatching2` function which returns absl::StatusOr<Matcher::ActionPtr>

*********************************************************************************************/
Http::FilterHeadersStatus RateLimitQuotaFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                              bool) {
  absl::StatusOr<Matcher::ActionPtr> match_result = requestMatching2(headers);

  // Request is not matched by any matchers. In this case, requests are ALLOWED by default (i.e.,
  // fail-open) and will not be reported to RLQS server.
  if (!match_result.ok()) {
    ENVOY_LOG(debug,
              "The request is not matched by any matchers: ", match_result.status().message());
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  // TODO(tyxia) dynamic_cast; target type must be a reference or pointer type to a defined class
  const RateLimitOnMactchAction* match_action =
      dynamic_cast<RateLimitOnMactchAction*>(match_result.value().get());
  // Try to generate the bucket id if the matching succeeded.
  auto ret = match_action->generateBucketId(*data_ptr_, factory_context_, visitor_);
  // Unable to generate the bucket id for this specific request. In this case, requests are ALLOWED
  // by default (i.e., fail-open) and will not be reported to RLQS server.
  if (!ret.ok()) {
    ENVOY_LOG(error, "Unable to generate the bucket id: ", ret.status().message());
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  BucketId bucket_id = ret.value();
  auto bucket_settings = match_action->bucketSettings();
  ASSERT(quota_bucket_ != nullptr);
  // The first request has been matched to the bucket successfully.
  if (quota_bucket_->find(bucket_id) == quota_bucket_->end()) {
    // TODO(tyxia) For the first matched request that doesn't have quota assignment from the RLQS
    // server, get the pre-configured rate limiting strategy from no assignment behavior in
    // the configuration.
    if (bucket_settings.has_no_assignment_behavior()) {
      // Retrieve the `blanket_rule` value from the config to decide if we want to fail-open or
      // fail-close.
      auto strategy = bucket_settings.no_assignment_behavior().fallback_rate_limit();
      if (strategy.blanket_rule() == RateLimitStrategy::ALLOW_ALL) {
        // TODO(tyxia) Implement the allow/deny interface
      }
    } else {
      ENVOY_LOG(error, "no assignment behavior for is not configured.");
      // We just use fail-open (i.e. ALLOW_ALL) here.
    }

    // Build the quota bucket element.
    BucketElement element;
    //  Create the gRPC client and start the stream on the first request.
    element.rate_limit_client_ = createRateLimitClient(factory_context_, config_->rlqs_server());
    auto status = element.rate_limit_client_->startStream(callbacks_->streamInfo());
    if (!status.ok()) {
      ENVOY_LOG(error, "Failed to start the gRPC stream: ", status.message());
      return Envoy::Http::FilterHeadersStatus::Continue;
    }
    // Send the usage report to RLQS immediately on the first time when the request is matched.
    // TODO(tyxia) Due to the lifetime concern of the bucket_id, I switch from pointer to absl::optional
    element.rate_limit_client_->sendUsageReport(bucket_id);

    // Set up the quota usage report method that sends the reports the RLS server periodically.
    // TODO(tyxia) If each bucket has its own timer callback, do we still need to build a list of
    // reports?????!!!
    // Create and enable the report timer callback on the first request.
    // TODO(tyxia) what is the behavior on the first request while waiting for the response
    element.send_reports_timer =
        factory_context_.mainThreadDispatcher().createTimer([&element]() -> void {
          ASSERT(element.rate_limit_client_ != nullptr);
          // TODO(tyxia) For periodical send behavior, we just pass in nullopt.
          element.rate_limit_client_->sendUsageReport(absl::nullopt);
        });
    // Set the reporting interval.
    const int64_t reporting_interval =
        PROTOBUF_GET_MS_REQUIRED(bucket_settings, reporting_interval);
    element.send_reports_timer->enableTimer(std::chrono::milliseconds(reporting_interval));

    // Store it into the buckets
    (*quota_bucket_)[bucket_id] = std::move(element);

    // TODO(tyxia) For the first request, we don't need to wait for the quota assignment from the
    // RLQS server because we already have the no_assignment behavior. Continue the filter chain
    // iteration.
    return Envoy::Http::FilterHeadersStatus::Continue;
  } else {
    // The existing bucket id is found.
    // BucketElement elem = (*quota_bucket_)[bucket_id];
    auto bucket_action = (*quota_bucket_)[bucket_id].bucket_action;
    if (bucket_action.has_quota_assignment_action()) {
      auto quota_action = bucket_action.quota_assignment_action();
      if (!quota_action.has_rate_limit_strategy()) {
        // Nothing to do probabaly.
      }

      // Retrieve the rate limiting stragtegy.
      RateLimitStrategy strategy = quota_action.rate_limit_strategy();
      if (strategy.has_blanket_rule() && strategy.blanket_rule() == RateLimitStrategy::ALLOW_ALL) {
        // TODO(tyxia) Keep track of #num of requests allowed, #num of requests denied.
        return Envoy::Http::FilterHeadersStatus::Continue;
      }
    }
  }

  rate_limit_client_->rateLimit(*this);

  return Envoy::Http::FilterHeadersStatus::Continue;
}

// TODO(tyxia) Return the std::unique_ptr<Action>; so that the caller can interpret the result
// and
absl::StatusOr<Matcher::ActionPtr>
RateLimitQuotaFilter::requestMatching2(const Http::RequestHeaderMap& headers) {
  // Initialize the data pointer on first use and reuse it for subsequent requests.
  // This avoids creating the data object for every request, which is expensive.
  if (data_ptr_ == nullptr) {
    if (callbacks_ != nullptr) {
      data_ptr_ = std::make_unique<Http::Matching::HttpMatchingDataImpl>(
          callbacks_->streamInfo().downstreamAddressProvider());
    } else {
      return absl::InternalError("Filter callback has not been initialized successfully yet.");
    }
  }

  if (matcher_ == nullptr) {
    return absl::InternalError("Matcher tree has not been initialized yet");
  } else {
    data_ptr_->onRequestHeaders(headers);
    // TODO(tyxia) This function should trigger the CEL expression matching. Here, we need to
    // implement the custom_matcher and factory, also statically register it so that CEL matching
    // will be triggered with its own match() method.
    auto match_result = Matcher::evaluateMatch<Http::HttpMatchingData>(*matcher_, *data_ptr_);

    if (match_result.match_state_ == Matcher::MatchState::MatchComplete) {
      if (match_result.result_) {
        // return the matched result for `on_match` case.
        return match_result.result_();
      } else {
        return absl::NotFoundError("The match was completed, no match found");
      }
    } else {
      // The returned state from `evaluateMatch` function is `MatchState::UnableToMatch` here.
      return absl::InternalError("Unable to match the request");
    }
  }
}

struct RateLimitResponseValue {
  // This request went above the configured limits for the rate limit filter.
  const std::string RateLimited = "request_rate_limited";
  // The rate limiter encountered a failure, and was configured to fail-closed.
  const std::string RateLimitError = "rate_limiter_error";
};
using RateLimitResponseDetails = ConstSingleton<RateLimitResponseValue>;

static inline Http::Code toErrorCode(uint64_t status) {
  const auto code = static_cast<Http::Code>(status);
  if (code >= Http::Code::BadRequest) {
    return code;
  }
  return Http::Code::TooManyRequests;
}

// TODO(tyxia) This is how we represent the RATELIMITED and ALLOWED
void RateLimitQuotaFilter::onComplete(const RateLimitQuotaBucketSettings& bucket_settings,
                                      RateLimitStatus status) {
  switch (status) {
  case RateLimitStatus::OK:
    break;
  case RateLimitStatus::OverLimit:
    // If the request has been `RateLimited`, send the local reply.
    auto deny_response_settings = bucket_settings.deny_response_settings();
    callbacks_->sendLocalReply(toErrorCode(deny_response_settings.http_status().code()),
                               std::string(deny_response_settings.http_body().value()), nullptr,
                               absl::nullopt, RateLimitResponseDetails::get().RateLimited);

    // The callback is nullptr at this moment.
    // Send local reply extra information want to be added in the local reply will add this
    // callback And it will be triggered when the local reply is constructed.
    // Response_header_to_add for ERROR Status Response for Error case
    // -- Rate limits exceed   response from envoy to downstream client
    // -- Like rate-limit-exceeded
    // Request for OK case
    // -- Request go through to be send to upstream
    break;
  }
}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
