Fixed a use-after-free in the dynamic modules network filter. A module that read the network read
buffer outside ``on_read``, for example from ``on_scheduled`` or ``on_http_callout_done``, could
read freed stack memory when the buffer delivered to ``on_read`` was a transient injected buffer
from ``inject_read_data``, the network ``ext_proc`` filter, or the ``tcp_bandwidth_limit`` filter.
Deferred access now resolves the connection read buffer, which stays valid for the connection
lifetime.
