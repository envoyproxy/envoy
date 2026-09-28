dynamic modules: fixed a use-after-free in the upstream HTTP to TCP bridge. A module that sent a
complete response from an event hook before the downstream request finished caused the router to
reset and destroy the bridge while the module hook was still running. The bridge now applies a
terminal response after the hook returns.
