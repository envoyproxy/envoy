
helm install eg oci://docker.io/envoyproxy/gateway-helm --version v0.0.0-latest -n envoy-gateway-system --create-namespace

kubectl wait --timeout=5m -n envoy-gateway-system deployment/envoy-gateway --for=condition=Available

kubectl apply -f https://github.com/envoyproxy/gateway/releases/download/latest/quickstart.yaml -n default

# With external loadbalancer support

export GATEWAY_HOST=$(kubectl get gateway/eg -o jsonpath='{.status.addresses[0].value}')

curl --verbose --header "Host: www.example.com" http://$GATEWAY_HOST/get

# Wihtout loadbalancer support 

export ENVOY_SERVICE=$(kubectl get svc -n envoy-gateway-system --selector=gateway.envoyproxy.io/owning-gateway-namespace=default,gateway.envoyproxy.io/owning-gateway-name=eg -o jsonpath='{.items[0].metadata.name}')

kubectl -n envoy-gateway-system port-forward service/${ENVOY_SERVICE} 8888:80 &

curl --verbose --header "Host: www.example.com" http://localhost:8888/get

# Clean up

kubectl delete -f https://github.com/envoyproxy/gateway/releases/download/{{< yaml-version >}}/quickstart.yaml --ignore-not-found=true

helm uninstall eg -n envoy-gateway-system
