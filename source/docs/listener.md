# Listener implementation

## Socket handling

Envoy uses the following procedure for creating sockets and assigning them to workers.

1. When a listener is created, a socket is pre-created for every worker on the main thread. This
   allows most errors to be caught early on in the listener creation process (e.g., bad socket
   option, unable to bind, etc.).
    * If using reuse_port, a unique socket is created for every worker.
    * If not using reuse_port, a unique socket is created for worker 0, and then that socket
      is duplicated for all other workers.
2. In the case of a listener update in which the address/socket is to be shared (i.e., a named
   listener that has configuration settings changed but listens on the same address will share
   sockets with the draining listener), all sockets are duplicated regardless of whether using
   reuse_port or not. This keeps the logic equivalent, and also makes sure that when using
   reuse_port no TCP connections are dropped during the drain process (in the race condition between
   assigning a queue and closing the listening socket on the old listener).
3. In the case of hot restart, all sockets are requested by worker index. If the old process has
   a match, the socket is duplicated and sent to the new process. Thus, hot restart and a listener
   update effectively share the same update process. This is so that no connections are dropped
   when using reuse_port.
4. A consequence of every listener having dedicated sockets (whether duplicated or not) is that
   a listener can close() its sockets when removed without concern for other listeners.

For more information, see `ListenSocketFactoryImpl` and its use within `ListenerImpl` and
`ListenerManagerImpl`.
