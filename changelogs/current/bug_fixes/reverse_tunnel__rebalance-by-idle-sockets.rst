Fixed the reverse tunnel responder spreading a node's new sockets across workers by the number of sockets
each worker had ever accepted for it, a count that never went down. As sockets were used or closed, a worker
could be left with no idle socket for a node while others held several, and requests on that worker failed with
``fallback_no_reverse_socket``. New sockets now go to the worker holding the fewest idle sockets for the node,
among the workers of the same upstream socket interface extension.
