Fixed a crash in ``envoy.watchdog.profile_action``: the first time its watchdog event fired, the
action read a factory context that had been destroyed when the guard dog finished construction.
