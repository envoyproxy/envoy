dynamic modules: fixed a use-after-free where the HTTP and network filter socket option byte value
getters returned a view into a ``std::vector`` that relocated its elements when a later option was
stored, dangling views the ABI promises stay valid for the filter lifetime. The options are now
kept in a ``std::deque`` with stable element addresses, and the getters return the latest value set
for an option rather than the first.
