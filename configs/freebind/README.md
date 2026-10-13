# Freebind testing

To manually validate the `IP_FREEBIND` behavior in Envoy, you can launch Envoy with
[freebind.yaml](freebind.yaml).

The listener free bind behavior can be verified with:

1. `envoy -c ./configs/freebind/freebind.yaml -l trace`
2. `sudo ifconfig lo:1 192.168.42.1/30 up`
3. `nc -v -l 0.0.0.0 10001`

To cleanup run `sudo ifconfig lo:1 down`.

TODO(htuch): Steps to verify upstream behavior.
