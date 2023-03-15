#!/bin/bash -e

CONFIG="IyBQTEVBU0UgRE8gTk9UIFNURUFMIFRIRVNFIChFUEhFTUVSQUwpIENSRURFTlRJQUxTIFdISUxFIFRIRVkgQVJFIEJFSU5HIFRFU1RFRCAtIFRIQU5LWU9VIQoKdmVyc2lvbjogMC4xCmxvZzoKICBmaWVsZHM6CiAgICBzZXJ2aWNlOiByZWdpc3RyeQpzdG9yYWdlOgogIGNhY2hlOgogICAgYmxvYmRlc2NyaXB0b3I6IGlubWVtb3J5CiAgczM6CiAgICBhY2Nlc3NrZXk6IDNFVkc3REpHUEQyM0FOMlIxTlpNCiAgICBzZWNyZXRrZXk6IFJNUW5OVmJxWW5pWHJnVk9QSTYybkJSNjlvMThhNE1HMWJlak5JZkUKICAgIHJlZ2lvbjogdXMtZWFzdC0yCiAgICByZWdpb25lbmRwb2ludDogaHR0cDovL3MzLnVzLWVhc3QtMi53YXNhYmlzeXMuY29tCiAgICBidWNrZXQ6IGVudm95LWRvY2tlci10ZXN0Cmh0dHA6CiAgYWRkcjogOjUwMDAKICBoZWFkZXJzOgogICAgWC1Db250ZW50LVR5cGUtT3B0aW9uczogW25vc25pZmZdCmhlYWx0aDoKICBzdG9yYWdlZHJpdmVyOgogICAgZW5hYmxlZDogdHJ1ZQogICAgaW50ZXJ2YWw6IDEwcwogICAgdGhyZXNob2xkOiAzCg=="

TMP_DOCKER_CONFIG=/tmp/docker-config.yml


create_config () {
    echo "$CONFIG" | base64 -d > "$TMP_DOCKER_CONFIG"
}

start_local_registry () {
    docker run -v "${TMP_DOCKER_CONFIG}:/etc/docker/registry/config.yml" -d -p 5000:5000 --name registry registry:2
}

pull_build_image () {
    docker pull localhost:5000/envoy-build-ubuntu:latest
    docker tag localhost:5000/envoy-build-ubuntu:latest envoyproxy/envoy-build-ubuntu:458cb49ca2013c0ccf057d00ad1d4407920c4e52
}


create_config
start_local_registry
pull_build_image
