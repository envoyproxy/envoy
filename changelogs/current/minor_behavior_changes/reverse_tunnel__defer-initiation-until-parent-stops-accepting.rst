During a hot restart, the downstream reverse-tunnel initiator
(``envoy.bootstrap.reverse_tunnel.downstream_socket_interface``) now defers dialing its first
reverse connections until the newly started child has asked the parent instance to stop accepting
new connections. Previously the child began dialing as soon as its workers started, before the
parent's listeners closed, so a child connection could be accepted by the still-listening parent
(observable when the initiator's target cluster points at a shared loopback listener). Fresh starts
and hot-restart-disabled runs are unaffected: initiation begins immediately as before.
