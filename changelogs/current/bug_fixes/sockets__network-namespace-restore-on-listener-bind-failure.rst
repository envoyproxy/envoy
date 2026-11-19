Fixed a listener with a ``network_namespace_filepath`` leaving the main thread in that network
namespace when setting up its socket failed, for example because the address was already in use.
The main thread then kept creating its sockets in that namespace, such as those of other listeners
and of xDS and health check connections. Worker threads started afterwards inherited it as well.
