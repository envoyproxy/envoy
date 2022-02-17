#include <codecvt>
#include <locale>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/thread.h"
#include "source/common/event/signal_impl.h"
#include "source/exe/main_common.h"
#include "source/exe/service_base.h"

#include "absl/debugging/symbolize.h"

// Logging macro for SCM
#define ENVOY_LOG_SCM(LOGGER, LEVEL, ...)                                                          \
  do {                                                                                             \
    LOGGER.log(::spdlog::source_loc{__FILE__, __LINE__, __func__}, LEVEL, __VA_ARGS__);            \
  } while (0)

namespace Envoy {

namespace {
DWORD Win32FromHResult(HRESULT value) { return value & ~0x80070000; }

} // namespace

ServiceBase* ServiceBase::service_static = nullptr;

ServiceBase::ServiceBase(DWORD controlsAccepted) : handle_(0) {
  status_.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  status_.dwCurrentState = SERVICE_START_PENDING;
  status_.dwControlsAccepted = controlsAccepted;
}

bool ServiceBase::TryRunAsService(ServiceBase& service) {
  // We need to be extremely defensive when programming between the start of the program until
  // `main_common` starts because the loggers have not been initialized
  //  so we do not have a good way to know what is happening if the program fails.
  service_static = &service;

  // The `SERVICE_TABLE_ENTRY` struct requires a volatile `LPSTR`
  char nullstr[1] = "";
  SERVICE_TABLE_ENTRYA service_table[] = {// Even though the service name is ignored for own process
                                          // services, it must be a valid string and cannot be 0.
                                          {nullstr, (LPSERVICE_MAIN_FUNCTIONA)ServiceMain},
                                          // Designates the end of table.
                                          {0, 0}};

  if (!::StartServiceCtrlDispatcherA(service_table)) {
    auto last_error = ::GetLastError();
    // There are two cases:
    // 1. The user is trying to run Envoy as a console app. In that the last error is
    // `ERROR_FAILED_SERVICE_CONTROLLER_CONNECT`. In that case we return false and let Envoy start
    // as normal.
    // 2. Other programmatic errors.
    if (last_error == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
      return false;
    } else {
      std::string error_msg{
          fmt::format("Could not dispatch Envoy to start as a service with error {}", last_error)};
      ENVOY_LOG_SCM(service_static->windows_event_logger_, spdlog::level::err, error_msg);
      PANIC(error_msg);
    }
  }
  return true;
}

DWORD ServiceBase::Start(std::vector<std::string> args) {
  // Run the server listener loop outside try/catch blocks, so that unexpected exceptions
  // show up as a core-dumps for easier diagnostics.
  absl::InitializeSymbolizer(args[0].c_str());
  std::shared_ptr<Envoy::MainCommon> main_common;

  // Initialize the server's main context under a try/catch loop and simply return `EXIT_FAILURE`
  // as needed. Whatever code in the initialization path that fails is expected to log an error
  // message so the user can diagnose.
  TRY_ASSERT_MAIN_THREAD {
    main_common = std::make_shared<Envoy::MainCommon>(args);
    Envoy::Server::Instance* server = main_common->server();
    if (!server->options().signalHandlingEnabled()) {
      // This means that the Envoy has not registered `ENVOY_SIGTERM`.
      // we need to manually enable it as we are going to use it to
      // handle close requests from `SCM`.
      sigterm_ = server->dispatcher().listenForSignal(ENVOY_SIGTERM, [server]() {
        ENVOY_LOG_MISC(warn, "caught ENVOY_SIGTERM");
        server->shutdown();
      });
    }
  }
  END_TRY
  catch (const Envoy::NoServingException& e) {
    return S_OK;
  }
  catch (const Envoy::MalformedArgvException& e) {
    ENVOY_LOG_SCM(service_static->windows_event_logger_, spdlog::level::err, e.what());
    return E_INVALIDARG;
  }
  catch (const Envoy::EnvoyException& e) {
    ENVOY_LOG_SCM(service_static->windows_event_logger_, spdlog::level::err, e.what());
    return E_FAIL;
  }

  return main_common->run() ? S_OK : E_FAIL;
}

void ServiceBase::Stop(DWORD control) {
  auto handler = Event::eventBridgeHandlersSingleton::get()[ENVOY_SIGTERM];
  if (!handler) {
    PANIC("No handler is registered to stop gracefully, aborting the program.");
  }

  char data[] = {'a'};
  Buffer::RawSlice buffer{data, 1};
  auto result = handler->writev(&buffer, 1);
  RELEASE_ASSERT(result.return_value_ == 1,
                 fmt::format("failed to write 1 byte: {}", result.err_->getErrorDetails()));
}

void ServiceBase::UpdateState(DWORD state, HRESULT errorCode, bool serviceError) {
  status_.dwCurrentState = state;
  if (FAILED(errorCode)) {
    if (!serviceError) {
      status_.dwWin32ExitCode = Win32FromHResult(errorCode);
    } else {
      status_.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
      status_.dwServiceSpecificExitCode = errorCode;
    }
  }
  SetServiceStatus();
}

void ServiceBase::SetServiceStatus() {
  RELEASE_ASSERT(service_static != nullptr, "Global pointer to service should not be null");
  if (!::SetServiceStatus(handle_, &status_)) {
    PANIC(
        fmt::format("Could not start StartServiceCtrlDispatcher with error {}", ::GetLastError()));
  }
}

void WINAPI ServiceBase::ServiceMain(DWORD argc, LPSTR* argv) {
  RELEASE_ASSERT(service_static != nullptr, "Global pointer to service should not be null");
  if (argc < 1 || argv == 0 || argv[0] == 0) {
    service_static->UpdateState(SERVICE_STOPPED, E_INVALIDARG, true);
    constexpr absl::string_view error_msg{"insufficient arguments provided"};
    ENVOY_LOG_SCM(service_static->windows_event_logger_, spdlog::level::err, error_msg);
    PANIC(error_msg);
  }

  service_static->handle_ = ::RegisterServiceCtrlHandlerA("ENVOY\0", Handler);
  if (service_static->handle_ == 0) {
    auto last_error = ::GetLastError();
    service_static->UpdateState(SERVICE_STOPPED, last_error, false);
    std::string error_msg{
        fmt::format("Could not register service control handler with error {}", last_error)};
    ENVOY_LOG_SCM(service_static->windows_event_logger_, spdlog::level::err, error_msg);
    PANIC(error_msg);
  }

  // Windows Services can get their arguments in two different ways
  // 1. With command line arguments that have been registered when the service gets created.
  // 2. With arguments coming from StartServiceA.
  // We merge the two cases into one vector of arguments that we provide to main common.
  auto cli = std::wstring(::GetCommandLineW());
  int envoyArgCount = 0;
  LPWSTR* argvEnvoy = CommandLineToArgvW(cli.c_str(), &envoyArgCount);
  std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
  std::vector<std::string> args;
  args.reserve(envoyArgCount + argc - 1);
  for (int i = 0; i < envoyArgCount; ++i) {
    args.emplace_back(converter.to_bytes(std::wstring(argvEnvoy[i])));
  }

  for (int i = 1; i < argc; i++) {
    args.emplace_back(std::string(argv[i]));
  }

  service_static->SetServiceStatus();
  service_static->UpdateState(SERVICE_RUNNING);
  DWORD rc = service_static->Start(args);
  service_static->UpdateState(SERVICE_STOPPED, rc, true);
}

void WINAPI ServiceBase::Handler(DWORD control) {
  // When the service control manager sends a control code to a service, it waits for the handler
  // function to return before sending additional control codes to other services.
  // The control handler should return as quickly as possible; if it does not return within 30
  // seconds, the SCM returns an error. If a service must do lengthy processing when the service is
  // executing the control handler, it should create a secondary thread to perform the lengthy
  // processing, and then return from the control handler.
  switch (control) {
  case SERVICE_CONTROL_SHUTDOWN:
  case SERVICE_CONTROL_PRESHUTDOWN:
  case SERVICE_CONTROL_STOP: {
    ENVOY_BUG(service_static->status_.dwCurrentState == SERVICE_RUNNING,
              "Attempting to stop Envoy service when it is not running");
    service_static->UpdateState(SERVICE_STOP_PENDING);
    service_static->Stop(control);
    break;
  }
  }
}
} // namespace Envoy
