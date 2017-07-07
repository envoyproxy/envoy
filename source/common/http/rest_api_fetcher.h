#pragma once

#include <chrono>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/cluster_manager.h"

namespace Envoy {
namespace Http {

/**
 * A helper base class used to fetch a REST API at a jittered periodic interval. Once initialize()
 * is called, the API will be fetched and events raised.
 */
class RestApiFetcher : public Http::AsyncClient::Callbacks {
protected:
  RestApiFetcher(Upstream::ClusterManager& cm, const std::string& remote_cluster_name,
                 Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
                 std::chrono::milliseconds refresh_interval);
  ~RestApiFetcher();

  /**
   * Start the fetch sequence. This should be called once.
   */
  void initialize();

  /**
   * This will be called when a fetch is about to happen. It should be overridden to fill the
   * request message with a valid request.
   */
  virtual void createRequest(Message& request) PURE;

  /**
   * This will be called when a 200 response is returned by the API with the response message.
   */
  virtual void parseResponse(const Message& response) PURE;

  /**
   * This will be called either in the success case or in the failure case for each fetch. It can
   * be used to hold common post request logic.
   */
  virtual void onFetchComplete() PURE;

  /**
   * This will be called if the fetch fails (either due to non-200 response, network error, etc.).
   * @param e supplies any exception data on why the fetch failed. May be nullptr.
   */
  virtual void onFetchFailure(const EnvoyException* e) PURE;

protected:
  const std::string remote_cluster_name_;
  Upstream::ClusterManager& cm_;

private:
  void refresh();
  void requestComplete();

  // Http::AsyncClient::Callbacks
  void onSuccess(Http::MessagePtr&& response) override;
  void onFailure(Http::AsyncClient::FailureReason reason) override;

  Runtime::RandomGenerator& random_;
  const std::chrono::milliseconds refresh_interval_;
  Event::TimerPtr refresh_timer_;
  Http::AsyncClient::Request* active_request_{};
};

} // namespace Http
} // namespace Envoy
