Fixed the dynamic module cluster delivering an async host selection inline, on the thread that
called ``envoy_dynamic_module_callback_cluster_lb_async_host_selection_complete``, which the Rust
SDK's ``EnvoyAsyncHostSelectionComplete::complete`` calls, when its load balancer had not yet served
a request with a downstream connection, such as the main thread's load balancer or a worker's that
only served async client streams. The router then ran on a module thread, and a completion made
inside ``envoy_dynamic_module_on_cluster_lb_choose_host`` reached the router before it was ready.
Envoy now delivers the result later, never inline, on the thread that owns the load balancer. It
drops a completion made inside ``envoy_dynamic_module_on_cluster_lb_choose_host`` when that call
answers synchronously, a second completion of a selection whose result it already delivered, unless
another selection with the same context pointer has started on the same load balancer by then, and
a completion for a load balancer that is already gone.
