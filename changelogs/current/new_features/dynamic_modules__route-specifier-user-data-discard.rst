Added the optional ``on_route_specifier_user_data_discard`` hook to the dynamic modules route
specifier, ``on_user_data_discard`` in the Rust SDK. Envoy calls it with the user data a module
recorded when the decision builds no route to carry it, such as a failed decision, a decision other
than ``Unspecified`` or a ``Hit`` from ``lookup_built_route``, so that a module can release what it
leased for the route. The ``user_data_discard`` statistic counts the values handed back.
