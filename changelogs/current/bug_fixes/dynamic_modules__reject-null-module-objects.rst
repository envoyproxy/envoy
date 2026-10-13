Fixed crashes and silent no-ops when a dynamic module returns a null object from an ``on_*_new``
hook. A null UDP listener filter config is now rejected at config load, a null cluster load balancer
no longer registers the host membership callback, and a null bootstrap extension is rejected at
load. The cluster constructor also checks the base construction status before running the module
hook.
