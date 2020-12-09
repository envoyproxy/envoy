#pragma once

#include <functional>
#include <string>

#include "exe/service_status.h"
#include "envoy/event/signal.h"

namespace Envoy {
class ServiceBase {
public:
  virtual ~ServiceBase() = default;

  static bool TryRunAsService(ServiceBase& service);

  ServiceBase(DWORD controlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN |
                                       SERVICE_CONTROL_PRESHUTDOWN);

  DWORD Start(std::vector<std::string> args, DWORD control);
  void Stop(DWORD control);

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
