In this example, we show how a [Redis filter](https://www.envoyproxy.io/docs/envoy/latest/configuration/network_filters/redis_proxy_filter) can be used with the Envoy proxy. The Envoy proxy [configuration](./envoy.yaml) includes a redis filter that routes egress requests to redis server.



# Usage
1. `docker-compose pull`
2. `docker-compose up --build`
3. Issue redis commands using your favourite redis client such as `redis-cli`

## Sample Output:

Use `redis-cli` to issue some redis commands and verify they are routed via Envoy:

```
> redis-cli -h localhost -p 1999 set foo foo
OK
> redis-cli -h localhost -p 1999 set bar bar
OK
> redis-cli -h localhost -p 1999 get foo
"foo"
> redis-cli -h localhost -p 1999 get bar
"bar"
```

Go to `http://localhost:8001/stats?usedonly&filter=redis.egress_redis.command` and verify the following stats

```
redis.egress_redis.command.get.total: 2
redis.egress_redis.command.set.total: 2
```