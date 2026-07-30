Fixed a lifetime bug in dynamic forward proxy async host selection when the cluster is removed
while lookup is still pending. Pending lookups are now cleaned up on DFP load balancer teardown,
and the router no longer resumes async completion through a stale cluster reference.
