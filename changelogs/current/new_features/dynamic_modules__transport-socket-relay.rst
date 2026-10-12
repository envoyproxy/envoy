Added dynamic module transport socket callbacks that let a module relay a TCP proxy session between
its downstream and upstream sockets outside Envoy's buffers, for example with ``splice(2)``. A
module can pair the two sockets by connection ID, count the relayed bytes in the stream info, read
into and write from the connection buffers directly, and close a connection with a reset.
