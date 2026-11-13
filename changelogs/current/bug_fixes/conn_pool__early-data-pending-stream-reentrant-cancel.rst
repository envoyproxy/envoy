Fixed a use-after-free in the upstream connection pool when a connection became ready for 0-RTT early
data (HTTP/3) and attaching one pending stream synchronously cancelled or failed another pending stream
that had not been attached yet. The pending stream queue now tolerates any item being removed while it
is being iterated.
