Fixed ``Network::Utility::execInNetworkNamespace`` to restore the calling thread's network
namespace when the callback throws. The restore now runs on scope exit, so a callback that throws
(for example an exception raised while binding a listener) no longer leaves the thread in the
target namespace if the caller catches the exception and keeps running.
