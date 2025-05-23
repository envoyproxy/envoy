# Running the Sandbox for reverse connections

## Steps to run sandbox

1. Build envoy with reverse connections feature:
   - ```./ci/run_envoy_docker.sh './ci/do_ci.sh bazel.release.server_only'```
2. Build envoy docker image:
   - ```docker build -f ci/Dockerfile-envoy-image -t envoy:latest .```
3. Launch test containers.
   - ```docker-compose -f examples/reverse_connection/docker-compose.yaml up```
4. The reverse example configuration in on-prem-envoy.yaml initiates 2 reverse connections per envoy thread to cloud envoy as shown in the listener config:

    ```yaml    
            reverse_connection_listener_config:
            "@type": type.googleapis.com/envoy.extensions.reverse_connection.reverse_connection_listener_config.v3.ReverseConnectionListenerConfig
            src_cluster_id: on-prem
            src_node_id: on-prem-node
            src_tenant_id: on-prem
            remote_cluster_to_conn_count:
            - cluster_name: cloud
                reverse_connection_count: 2
    ```

5. Verify that the reverse connections are established by sending requests to the reverse conn API:
   On on-prem envoy, the expected output is a list of envoy clusters to which reverse connections have been
   established, in this instance, just "cloud".

    ```bash
    [basundhara.c@basundhara-c ~]$ curl localhost:9000/reverse_connections               
    {"accepted":[],"connected":["cloud"]} 
    ``` 
   On cloud-envoy, the expected output is a list on nodes that have initiated reverse connections to it,
   in this case, "on-prem-node".
   
   ```bash
    [basundhara.c@basundhara-c ~]$ curl localhost:9001/reverse_connections                  
    {"accepted":["on-prem-node"],"connected":[]}
   ``` 

6. Test reverse connection:
   - Perform http request for the service behind on-prem envoy, to cloud-envoy. This request will be sent
   over a reverse connection.

    ```bash
    [basundhara.c@basundhara-c ~]$ curl -H "x-remote-node-id: on-prem-node" -H "x-dst-cluster-uuid: on-prem" http://localhost:8081/on_prem_service  
    Server address: 172.21.0.3:80
    Server name: 281282e5b496
    Date: 26/Nov/2024:04:04:03 +0000
    URI: /on_prem_service
    Request ID: 726030e25e52db44a6c06061c4206a53
    ```
