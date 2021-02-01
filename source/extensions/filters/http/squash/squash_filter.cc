#include "extensions/filters/http/squash/squash_filter.h"

#include <memory>

#include "envoy/extensions/filters/http/squash/v3/squash.pb.h"
#include "envoy/http/codes.h"

#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/common/logger.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/json/json_loader.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "absl/container/fixed_array.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Squash {

using std::placeholders::_1;

const std::regex SquashFilterConfig::ENV_REGEX("\\{\\{ (\\w+) \\}\\}");

const std::string SquashFilter::POST_ATTACHMENT_PATH = "/api/v2/debugattachment/";
const std::string SquashFilter::SERVER_AUTHORITY = "squash-server";
const std::string SquashFilter::ATTACHED_STATE = "attached";
const std::string SquashFilter::ERROR_STATE = "error";

SquashFilterConfig::SquashFilterConfig(
    const envoy::extensions::filters::http::squash::v3::Squash& proto_config,
    Upstream::ClusterManager& cluster_manager)
    : cluster_name_(proto_config.cluster()),
      attachment_json_(getAttachment(proto_config.attachment_template())),
      attachment_timeout_(PROTOBUF_GET_MS_OR_DEFAULT(proto_config, attachment_timeout, 60000)),
      attachment_poll_period_(
          PROTOBUF_GET_MS_OR_DEFAULT(proto_config, attachment_poll_period, 1000)),
      request_timeout_(PROTOBUF_GET_MS_OR_DEFAULT(proto_config, request_timeout, 1000)) {

  if (!cluster_manager.clusters().hasCluster(cluster_name_)) {
    throw EnvoyException(
        fmt::format("squash filter: unknown cluster '{}' in squash config", cluster_name_));
  }
}

std::string SquashFilterConfig::getAttachment(const ProtobufWkt::Struct& attachment_template) {
  ProtobufWkt::Struct attachment_json(attachment_template);
  updateTemplateInStruct(attachment_json);
  return MessageUtil::getJsonStringFromMessageOrDie(attachment_json);
}

void SquashFilterConfig::updateTemplateInStruct(ProtobufWkt::Struct& attachment_template) {
  for (auto& value_it : *attachment_template.mutable_fields()) {
    auto& curvalue = value_it.second;
    updateTemplateInValue(curvalue);
  }
}

void SquashFilterConfig::updateTemplateInValue(ProtobufWkt::Value& curvalue) {
  switch (curvalue.kind_case()) {
  case ProtobufWkt::Value::kStructValue: {
    updateTemplateInStruct(*curvalue.mutable_struct_value());
    break;
  }
  case ProtobufWkt::Value::kListValue: {
    ProtobufWkt::ListValue& values = *curvalue.mutable_list_value();
    for (int i = 0; i < values.values_size(); i++) {
      updateTemplateInValue(*values.mutable_values(i));
    }
    break;
  }
  case ProtobufWkt::Value::kStringValue: {
    curvalue.set_string_value(replaceEnv(curvalue.string_value()));
    break;
  }
  case ProtobufWkt::Value::KIND_NOT_SET:
  case ProtobufWkt::Value::kNullValue:
  case ProtobufWkt::Value::kBoolValue:
  case ProtobufWkt::Value::kNumberValue: {
    // Nothing here... we only need to transform strings
  }
  }
}

/*
 This function interpolates environment variables in a string template.
 To interpolate an environment variable named ENV, add '{{ ENV }}' (without the
  quotes, with the spaces) to the template string.

  See api/envoy/config/filter/http/squash/v2/squash.proto for the motivation on why this is needed.
*/
std::string SquashFilterConfig::replaceEnv(const std::string& attachment_template) {
  std::string s;

  auto end_last_match = attachment_template.begin();

  auto replaceEnvVarInTemplateCallback =
      [&s, &attachment_template,
       &end_last_match](const std::match_results<std::string::const_iterator>& match) {
        auto start_match = attachment_template.begin() + match.position(0);

        s.append(end_last_match, start_match);

        std::string envar_name = match[1].str();
        const char* envar_value = std::getenv(envar_name.c_str());
        if (envar_value == nullptr) {
          ENVOY_LOG(warn, "Squash: no environment variable named {}.", envar_name);
        } else {
          s.append(envar_value);
        }
        end_last_match = start_match + match.length(0);
      };

  std::sregex_iterator begin(attachment_template.begin(), attachment_template.end(), ENV_REGEX),
      end;
  std::for_each(begin, end, replaceEnvVarInTemplateCallback);
  s.append(end_last_match, attachment_template.end());

  return s;
}

SquashFilter::SquashFilter(SquashFilterConfigSharedPtr config, Upstream::ClusterManager& cm)
    : config_(config), is_squashing_(false), attachment_poll_period_timer_(nullptr),
      attachment_timeout_timer_(nullptr), in_flight_request_(nullptr),
      create_attachment_callback_(std::bind(&SquashFilter::onCreateAttachmentSuccess, this, _1),
                                  std::bind(&SquashFilter::onCreateAttachmentFailure, this, _1)),
      check_attachment_callback_(std::bind(&SquashFilter::onGetAttachmentSuccess, this, _1),
                                 std::bind(&SquashFilter::onGetAttachmentFailure, this, _1)),
      cm_(cm), decoder_callbacks_(nullptr) {}

SquashFilter::~SquashFilter() = default;

void SquashFilter::onDestroy() { cleanup(); }

Http::FilterHeadersStatus SquashFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  // Check for squash header
  if (headers.get(Http::Headers::get().XSquashDebug).empty()) {
    return Http::FilterHeadersStatus::Continue;
  }

  ENVOY_LOG(debug, "Squash: Holding request and requesting debug attachment");

  Http::RequestMessagePtr request(new Http::RequestMessageImpl());
  request->headers().setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
  request->headers().setReferencePath(POST_ATTACHMENT_PATH);
  request->headers().setReferenceHost(SERVER_AUTHORITY);
  request->headers().setReferenceMethod(Http::Headers::get().MethodValues.Post);
  request->body().add(config_->attachmentJson());

  is_squashing_ = true;
  const auto thread_local_cluster = cm_.getThreadLocalCluster(config_->clusterName());
  if (thread_local_cluster != nullptr) {
    in_flight_request_ = thread_local_cluster->httpAsyncClient().send(
        std::move(request), create_attachment_callback_,
        Http::AsyncClient::RequestOptions().setTimeout(config_->requestTimeout()));
  }

  if (in_flight_request_ == nullptr) {
    ENVOY_LOG(debug, "Squash: can't create request for squash server");
    is_squashing_ = false;
    return Http::FilterHeadersStatus::Continue;
  }

  attachment_timeout_timer_ =
      decoder_callbacks_->dispatcher().createTimer([this]() -> void { doneSquashing(); });
  attachment_timeout_timer_->enableTimer(config_->attachmentTimeout(),
                                         &decoder_callbacks_->scope());
  // Check if the timer expired inline.
  if (!is_squashing_) {
    return Http::FilterHeadersStatus::Continue;
  }

  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus SquashFilter::decodeData(Buffer::Instance&, bool) {
  if (is_squashing_) {
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus SquashFilter::decodeTrailers(Http::RequestTrailerMap&) {
  if (is_squashing_) {
    return Http::FilterTrailersStatus::StopIteration;
  }
  return Http::FilterTrailersStatus::Continue;
}

void SquashFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

void SquashFilter::onCreateAttachmentSuccess(Http::ResponseMessagePtr&& m) {
  in_flight_request_ = nullptr;

  // Get the config object that was created
  if (Http::Utility::getResponseStatus(m->headers()) != enumToInt(Http::Code::Created)) {
    ENVOY_LOG(debug, "Squash: can't create attachment object. status {} - not squashing",
              m->headers().getStatusValue());
    doneSquashing();
  } else {
    std::string debug_attachment_id;
    try {
      Json::ObjectSharedPtr json_config = getJsonBody(std::move(m));
      debug_attachment_id =
          json_config->getObject("metadata", true)->getString("name", EMPTY_STRING);
    } catch (Json::Exception&) {
      debug_attachment_id = EMPTY_STRING;
    }

    if (debug_attachment_id.empty()) {
      ENVOY_LOG(debug, "Squash: failed to parse debug attachment object - check server settings.");
      doneSquashing();
    } else {
      debug_attachment_path_ = POST_ATTACHMENT_PATH + debug_attachment_id;
      pollForAttachment();
    }
  }
}

void SquashFilter::onCreateAttachmentFailure(Http::AsyncClient::FailureReason) {
  // in_flight_request_ will be null if we are called inline of async client send()
  bool request_created = in_flight_request_ != nullptr;
  in_flight_request_ = nullptr;

  // No retries here, as we couldn't create the attachment object.
  if (request_created) {
    // Cleanup not needed if onFailure called inline in async client send, as this means that
    // decodeHeaders is down the stack and will return Continue.
    doneSquashing();
  }
}

void SquashFilter::onGetAttachmentSuccess(Http::ResponseMessagePtr&& m) {
  in_flight_request_ = nullptr;

  std::string attachmentstate;
  try {
    Json::ObjectSharedPtr json_config = getJsonBody(std::move(m));
    attachmentstate = json_config->getObject("status", true)->getString("state", EMPTY_STRING);
  } catch (Json::Exception&) {
    // No state yet.. leave it empty for the retry logic.
  }

  if (attachmentstate == ATTACHED_STATE || attachmentstate == ERROR_STATE) {
    doneSquashing();
  } else {
    // Always schedule a retry. The attachment_timeout_timer_ will stop the retry loop when it
    // expires.
    scheduleRetry();
  }
}

void SquashFilter::onGetAttachmentFailure(Http::AsyncClient::FailureReason) {
  in_flight_request_ = nullptr;
  scheduleRetry();
}

void SquashFilter::scheduleRetry() {
  if (attachment_poll_period_timer_.get() == nullptr) {
    attachment_poll_period_timer_ =
        decoder_callbacks_->dispatcher().createTimer([this]() -> void { pollForAttachment(); });
  }
  attachment_poll_period_timer_->enableTimer(config_->attachmentPollPeriod(),
                                             &decoder_callbacks_->scope());
}

void SquashFilter::pollForAttachment() {
  Http::RequestMessagePtr request(new Http::RequestMessageImpl());
  request->headers().setReferenceMethod(Http::Headers::get().MethodValues.Get);
  request->headers().setReferencePath(debug_attachment_path_);
  request->headers().setReferenceHost(SERVER_AUTHORITY);

  const auto thread_local_cluster = cm_.getThreadLocalCluster(config_->clusterName());
  if (thread_local_cluster != nullptr) {
    in_flight_request_ = thread_local_cluster->httpAsyncClient().send(
        std::move(request), check_attachment_callback_,
        Http::AsyncClient::RequestOptions().setTimeout(config_->requestTimeout()));
  } else {
    scheduleRetry();
  }
  // No need to check if in_flight_request_ is null as onFailure will take care of
  // cleanup.
}

void SquashFilter::doneSquashing() {
  cleanup();
  decoder_callbacks_->continueDecoding();
}

void SquashFilter::cleanup() {
  is_squashing_ = false;

  if (attachment_poll_period_timer_) {
    attachment_poll_period_timer_->disableTimer();
    attachment_poll_period_timer_.reset();
  }

  if (attachment_timeout_timer_) {
    attachment_timeout_timer_->disableTimer();
    attachment_timeout_timer_.reset();
  }

  if (in_flight_request_ != nullptr) {
    in_flight_request_->cancel();
    in_flight_request_ = nullptr;
  }

  debug_attachment_path_ = EMPTY_STRING;
}

Json::ObjectSharedPtr SquashFilter::getJsonBody(Http::ResponseMessagePtr&& m) {
  return Json::Factory::loadFromString(m->bodyAsString());
}

} // namespace Squash
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
