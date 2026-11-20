Fixed a use-after-free in the ``kafka_mesh`` network filter (contrib). A Fetch request that finished
while its downstream connection was closing delivered its answer through a dispatcher callback that
held a raw pointer to the request and called the filter directly, so the callback ran on the freed
request and filter. The callback now only runs if the request is still alive, and reaches the filter
only while the filter has not abandoned the request.
