# Original destination cluster configuration and testing

An original destination cluster forwards requests to the same destination
the request was going to before being redirected to Envoy using an
iptables REDIRECT rule. `proxy_config.json` contains an example Envoy
configuration demonstrating the use of an original destination
cluster. `netns_setup.sh` and `netns_cleanup.sh` are provided as
examples for setting up and cleaning up, respectively, a network
namespace and the required iptables rule to redirect traffic to Envoy.

# Setting up

`netns_setup.sh` takes two arguments: the name of the new network
namespace and the prefix that is to be redirected. Envoy listener port
is set to 10000, which matches the configuration in
`proxy_config.json`.

This creates a network namespace `ns1` and redirects traffic from
there to Envoy listening on port 10000 if the destination address of
the traffic matches `173.194.222.0/24` :

```
sudo ./configs/original-dst-cluster/netns_setup.sh ns1 173.194.222.0/24
```

# Building and running Envoy

Build Envoy with debug options, so that the behavior can be better
observed from the logs:

```
bazel build //source/exe:envoy-static -c dbg
```

Then you should run Envoy with the provided example configuration:

```
bazel-out/local-dbg/bin/source/exe/envoy-static -c configs/original-dst-cluster/proxy_config.json -l debug
```

When running you should see periodical messages like `Cleaning up
stale original dst hosts.`

# Generating traffic

Next we generate traffic from the new network namespace hitting the
redirect rule. Run this from another terminal:

```
sudo ip netns exec ns1 curl -v 173.194.222.106:80
```

Most likely you'll see `301 Moved` in the curl response. In the rare
case of upstream connection timeout you'll see `503 Service
Unavailable` instead. The connection timeout setting on the
proxy_config.json is set to 6 seconds to make this less likely, but if
no host with the destination address exist then you will get this
response no matter how long the timeout setting.

You should see lines with `Adding host 173.194.222.106:80` being
logged by each Envoy thread, followed by `Keeping active host
173.194.222.106:80` and eventually `Removing stale host
173.194.222.106:80`, again multiple times, once from each Envoy
thread.

# Cleaning up

To properly remove the added network namespace and the iptables
configuration run `netns_cleanup.sh` with the same arguments as
the setup before:

```
sudo ./configs/original-dst-cluster/netns_cleanup.sh ns1 173.194.222.0/24
```

Finally, stop Envoy with `^C`.
