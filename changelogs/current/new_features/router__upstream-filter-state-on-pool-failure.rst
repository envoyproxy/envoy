The upstream connection filter state is now recorded on the connection pool failure path as well as
on the pool ready path, so filter state written by an upstream transport socket is readable through
``%UPSTREAM_FILTER_STATE%`` when the upstream connection fails. Previously it was only readable when
the connection succeeded.
