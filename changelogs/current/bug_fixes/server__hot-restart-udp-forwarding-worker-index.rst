Fixed an out-of-bounds read that could crash a hot restart child started with a lower ``--concurrency`` than its
parent: UDP/QUIC packets the parent forwarded during the hot restart carried the parent's worker index, which the
child used to look up one of its own workers without checking that it had that many. A worker index the child
does not have is now mapped onto the child's workers modulo its worker count, which for the deterministic QUIC
connection ID generator is the worker the child itself selects when the parent's worker count is a multiple of
the child's.
