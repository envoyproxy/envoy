Hot restart: at the end of the drain period the new process no longer takes over the inherited UDP listeners as soon as
it has sent the terminate request to the parent. It retries the request while the parent's socket is full, and starts
reading those sockets once the parent has exited, so the two processes do not read them at the same time; the wait is
capped at two seconds, after which it logs (at error level if the parent never accepted the request) and proceeds. This
change can be reverted by setting the runtime guard ``envoy.reloadable_features.hot_restart_wait_for_parent_exit`` to
``false``.
