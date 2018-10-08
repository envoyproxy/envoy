Is there a way to disable circuit breaking?
===========================================

Envoy comes with :ref:`certain defaults <envoy_api_msg_cluster.CircuitBreakers.Thresholds>`
for each kind of circuit breaking. Currently, there isn't a switch to turn
circuit breaking off completely; however, you could achieve a similar behavior
by setting these thresholds very high, for example, to `std::numeric_limits<uint32_t>::max()`.
