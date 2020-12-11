#pragma once

#include <functional>
#include <string>

#include "envoy/event/signal.h"

#include "exe/service_status.h"

namespace Envoy {
class ServiceBase {
public:
  virtual ~ServiceBase() = default;
  /**
   * Try to register Envoy as a service. For this we register `ServiceMain` and `Handler`
   * @param ServiceBase instance of the service.
   * @return false if Envoy can not be registered to run as service.
   */
  static bool TryRunAsService(ServiceBase& service);

  ServiceBase(DWORD controlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN |
                                       SERVICE_CONTROL_PRESHUTDOWN);
  /**
   * Start the service.
   * @return exit code of Envoy.
   */
  DWORD Start(std::vector<std::string> args);

  /**
   * Stop the service.
   * @param control the control code Service Control Manager requested.
   */
  void Stop(DWORD control);

  /**
   * Update the state of the service.
   * @param state New service state.
   * @param errorCode Last error code encountered by Envoy.
   * @param serviceError If the error is coming from the service or a Win32 interaction.
   */
  void UpdateState(DWORD state, HRESULT errorCode = S_OK, bool serviceError = true);

private:
  void SetServiceStatus();

  static void WINAPI ServiceMain(DWORD argc, LPSTR* argv);

  static void WINAPI Handler(DWORD control);

  static ServiceBase* service_static;
  SERVICE_STATUS_HANDLE handle_;
  ServiceStatus status_;
  Event::SignalEventPtr sigterm_;
};
} // namespace Envoy
