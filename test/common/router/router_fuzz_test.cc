#include "test/common/router/router_fuzz.pb.h"
#include "test/common/router/router_fuzz.pb.validate.h"
#include "test/common/router/router_lib.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"

namespace Envoy {
namespace Router {

/* class RouterTestFilter : public Filter { */
/* public: */
/*   using Filter::Filter; */
/*   // Filter */
/*   RetryStatePtr createRetryState(const RetryPolicy&, Http::RequestHeaderMap&, */
/*                                  const Upstream::ClusterInfo&, const VirtualCluster*, */
/*                                  Runtime::Loader&, Random::RandomGenerator&, Event::Dispatcher&,
 */
/*                                  Upstream::ResourcePriority) override { */
/*     // in the normal router tests there is a new RouterTestFilter in every unit test, but since
 * this */
/*     // is being run in a loop there may be more than one */
/*     if (retry_state_ == nullptr) { */
/*       retry_state_ = new NiceMock<MockRetryState>(); */
/*     } */
/*     if (reject_all_hosts_) { */
/*       // Set up RetryState to always reject the host */
/*       ON_CALL(*retry_state_, shouldSelectAnotherHost(_)).WillByDefault(Return(true)); */
/*     } */
/*     return RetryStatePtr{retry_state_}; */
/*   } */

/*   const Network::Connection* downstreamConnection() const override { */
/*     return &downstream_connection_; */
/*   } */

/*   NiceMock<Network::MockConnection> downstream_connection_; */
/*   MockRetryState* retry_state_{}; */
/*   bool reject_all_hosts_ = false; */
/* }; */

class RouterFuzzTest : public RouterTestLib {
public:
  RouterFuzzTest() : RouterTestLib(false, false, Protobuf::RepeatedPtrField<std::string>{}) {
    EXPECT_CALL(callbacks_, activeSpan()).WillRepeatedly(ReturnRef(span_));
  };

  bool request_end_stream_{false};
  bool response_end_stream_{false};

  void streamRequest(test::common::router::DirectionalAction action) {
    // don't send any messages once end_stream has been sent
    if (request_end_stream_)
      return;

    switch (action.response_action_selector_case()) {
    case (test::common::router::DirectionalAction::kHeaders): {
      auto request_headers = Fuzz::fromHeaders<Http::TestRequestHeaderMapImpl>(action.headers());
      router_.decodeHeaders(request_headers, action.end_stream());
      break;
    }
    case (test::common::router::DirectionalAction::kData): {
      Buffer::OwnedImpl data;
      router_.decodeData(data, action.end_stream());
      break;
    }
    case (test::common::router::DirectionalAction::kTrailers): {
      auto request_trailers = Fuzz::fromHeaders<Http::TestRequestTrailerMapImpl>(action.trailers());
      router_.decodeTrailers(request_trailers);
      break;
    }
    default:
      break;
    }

    request_end_stream_ = action.end_stream();
  }

  void streamResponse(Http::ResponseDecoder* decoder,
                      test::common::router::DirectionalAction action) {
    // don't send any messages once end_stream has been sent
    if (response_end_stream_)
      return;

    switch (action.response_action_selector_case()) {
    case (test::common::router::DirectionalAction::kHeaders): {
      auto response_headers = Fuzz::fromHeaders<Http::TestResponseHeaderMapImpl>(action.headers());
      std::unique_ptr<Http::ResponseHeaderMap> headers(
          new Http::TestResponseHeaderMapImpl(response_headers));
      decoder->decodeHeaders(std::move(headers), action.end_stream());
      break;
    }
    case (test::common::router::DirectionalAction::kData): {
      Buffer::OwnedImpl data;
      decoder->decodeData(data, action.end_stream());
      break;
    }
    case (test::common::router::DirectionalAction::kTrailers): {
      auto response_trailers =
          Fuzz::fromHeaders<Http::TestResponseTrailerMapImpl>(action.trailers());
      std::unique_ptr<Http::ResponseTrailerMap> trailers(
          new Http::TestResponseTrailerMapImpl(response_trailers));
      decoder->decodeTrailers(std::move(trailers));
      break;
    }
    default:
      break;
    }

    response_end_stream_ = action.end_stream();
  }

  void replay(const test::common::router::RouterTestCase& input) {
    ENVOY_LOG_MISC(info, "{}", input.DebugString());
    // Set up the stream and send the initial headers
    NiceMock<Http::MockRequestEncoder> encoder;
    Http::ResponseDecoder* response_decoder = nullptr;
    ON_CALL(cm_.conn_pool_, newStream(_, _))
        .WillByDefault(Invoke(
            [&](Http::ResponseDecoder& decoder,
                Http::ConnectionPool::Callbacks& callbacks) -> Http::ConnectionPool::Cancellable* {
              response_decoder = &decoder;
              callbacks.onPoolReady(encoder, cm_.conn_pool_.host_, upstream_stream_info_);
              return nullptr;
            }));

    // This creates the response timeout timer.
    expectResponseTimerCreate();

    // Kick off the request with some default headers.
    Http::TestRequestHeaderMapImpl headers{{"x-envoy-retry-on", "5xx"},
                                           {"x-envoy-internal", "true"},
                                           {"x-envoy-upstream-rq-timeout-ms", "200"}};
    HttpTestUtility::addDefaultHeaders(headers);
    router_.decodeHeaders(headers, false);

    for (const auto& action : input.actions()) {
      switch (action.action_selector_case()) {
      case test::common::router::Action::kStreamAction: {
        ENVOY_LOG_MISC(info, "Stream Action");

        switch (action.stream_action().stream_action_selector_case()) {
        case test::common::router::StreamAction::kRequest: {
          ENVOY_LOG_MISC(info, "Request action");
          streamRequest(action.stream_action().request());
          break;
        }
        case test::common::router::StreamAction::kResponse: {
          ENVOY_LOG_MISC(info, "Response action");
          streamResponse(response_decoder, action.stream_action().response());
          break;
        }
        default:
          break;
        }

        break;
      }
      case test::common::router::Action::kAdvanceTime: {
        ENVOY_LOG_MISC(info, "Advance time");
  Http::ResponseHeaderMapPtr response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "200"}});
  response_decoder->decodeHeaders(std::move(response_headers), false);

        // The upstream timeout is 200ms.
        test_time_.timeSystem().advanceTimeWait(std::chrono::milliseconds(201));

        // This seems to cause an upstream timeout?
        response_timeout_->invokeCallback();
        break;
      }
      case test::common::router::Action::kForceRetry: {
        ENVOY_LOG_MISC(info, "503 retry");
        // Expect that the router will retry after getting the 503
        router_.retry_state_->expectHeadersRetry();
        ENVOY_LOG_MISC(info, "expectheaders done");
        Http::ResponseHeaderMapPtr response_headers(
            new Http::TestResponseHeaderMapImpl{{":status", "503"}});
        ENVOY_LOG_MISC(info, "created headers");
        if (response_decoder == nullptr) {
          ENVOY_LOG_MISC(info, "that boy null");
          return;
        }
        response_decoder->decodeHeaders(std::move(response_headers), true);
        ENVOY_LOG_MISC(info, "decoded headers");
        router_.retry_state_->callback_();
        ENVOY_LOG_MISC(info, "called callback");

        // Reset the expectation and send a normal response. It is necessary to send the normal
        // response before continuing iteration because the RouterTestFilter fails an assertion if
        // it does not get a response to all upstream requests before its destruction.
        EXPECT_CALL(*router_.retry_state_, shouldRetryHeaders(_, _)).WillOnce(Return(RetryStatus::No));
        Http::ResponseHeaderMapPtr response_headers2(
            new Http::TestResponseHeaderMapImpl{{":status", "200"}});
        EXPECT_CALL(cm_.conn_pool_.host_->outlier_detector_, putHttpResponseCode(200));
        response_decoder->decodeHeaders(std::move(response_headers2), true);
        break;
      }
      default:
        break;
      }
    }
  }
};

DEFINE_PROTO_FUZZER(const test::common::router::RouterTestCase& input) {
  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(info, "ProtoValidationException: {}", e.what());
    return;
  }

  RouterFuzzTest router_test{};
  router_test.replay(input);
}

} // namespace Router
} // namespace Envoy
