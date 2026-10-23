Added the new HTTP priority load shed filter
:ref:`envoy.filters.http.priority_load_shed <extension_envoy.filters.http.priority_load_shed>`,
which maps request header integer ranges to overload manager load shed points. It supports
fallback shedding via a default load shed point.
