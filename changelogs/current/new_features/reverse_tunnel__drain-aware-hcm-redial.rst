Added an opt-in ``enable_drain_with_goaway`` field to the reverse-tunnel drain-aware HTTP connection
manager. When enabled, a peer-initiated GOAWAY on a reverse tunnel makes the initiator drop the
draining tunnel and dial a replacement, restoring capacity before the old tunnel closes while its
in-flight streams finish. Default off, so existing listeners are unaffected.
