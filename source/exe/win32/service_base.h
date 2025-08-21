#pragma once

#include <processenv.h>
#include <shellapi.h>
#include <winsvc.h>

#include <functional>
#include <string>

#include "envoy/event/signal.h"

#include "source/common/common/win32/event_logger_impl.h"
#include "source/exe/service_status.h"

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

  ServiceBase(DWORD controlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PRESHUTDOWN);
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
  // You might be wondering why we use a raw logger here. The reason is that
  // this code is doing work before we parse the command line and the loggers have
  // not be initialized. When Envoy is running as service the user might not have access to
  // the standard error which makes it really hard to diagnose start up failures with
  // just the error code.
  Logger::WindowsEventLogger windows_event_logger_{"win32-scm-logger"};
};
} // namespace Envoy
