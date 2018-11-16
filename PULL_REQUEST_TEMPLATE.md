
### <a name="title"></a>Issue 4546: Refactor FD with a general I/O Handle

*Description*:

Introduce an abstract I/O Handle and one derivative for sockets, the IoSocketHandle.
Preserve fd semantics in all socket-related system calls with ioHandle->fd(). In this
initial PR, all calls which take an fd retain their same parameters.

*Risk Level*:

Low

*Testing*:

Everything covered by bazel test //test...

