Fixed a draining hot restart parent forwarding packets of QUIC connections it had just closed to the new process, which
does not know those connections and answered them with a stateless reset. Packets for connection IDs in the dispatcher's
time-wait list are now left to that list, which re-sends the stored ``CONNECTION_CLOSE``, and only packets for unknown
connections are forwarded. This change can be reverted by setting the runtime guard
``envoy.reloadable_features.quic_hot_restart_time_wait_before_forward`` to ``false``.
