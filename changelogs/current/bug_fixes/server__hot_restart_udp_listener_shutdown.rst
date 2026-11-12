Fixed a crash on process exit when a UDP listener owns server-dependent resources, such as a QUIC-LB
connection ID generator using SDS. UDP forwarding now releases its retained listener configurations during server
shutdown, before the server resources they depend on are destroyed.
