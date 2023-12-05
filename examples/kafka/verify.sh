#!/bin/bash -e

export NAME=kafka
export PORT_PROXY="${KAFKA_PORT_PROXY:-11100}"
export PORT_ADMIN="${KAFKA_PORT_ADMIN:-11101}"

# Explicitly specified the service want to start, since the `kafka-client` is expected to
# not start.
UPARGS="proxy kafka-server zookeeper"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

kafka_client () {
    "${DOCKER_COMPOSE[@]}" run --rm kafka-client "$@"
}

TOPIC="envoy-kafka-broker"

MESSAGE="Welcome to Envoy and Kafka broker filter!"

run_log "Create a Kafka topic"
kafka_client kafka-topics --bootstrap-server proxy:10000 --create --topic $TOPIC

run_log "Check the Kafka topic"
kafka_client kafka-topics --bootstrap-server proxy:10000 --list | grep $TOPIC

run_log "Send a message using the Kafka producer"
kafka_client /bin/bash -c " \
    echo $MESSAGE \
    | kafka-console-producer --request-required-acks 1 --broker-list proxy:10000 --topic $TOPIC"

run_log "Receive a message using the Kafka consumer"
kafka_client kafka-console-consumer --bootstrap-server proxy:10000 --topic $TOPIC --from-beginning --max-messages 1 | grep "$MESSAGE"

run_log "Check admin kafka_broker stats"

# This function verifies whether a given metric exists and has a value > 0.
has_metric_with_at_least_1 () {
    local stat response value
    stat="$1"
    shift
    response=$(_curl "http://localhost:${PORT_ADMIN}/stats?filter=${stat}")
    # Extract number from rows like 'kafka.kafka_broker.request.api_versions_request: 123'.
    value=$(echo "${response}" | grep "${stat}:" | cut -f2 -d':' | tr -d ' ')
    re='^[0-9]+$'
    [[ ${value} =~ ${re} && ${value} -gt 0 ]] || {
        echo "ERROR: metric check for [${stat}]" >&2
        echo "EXPECTED: numeric value greater than 0" >&2
        echo "RECEIVED:" >&2
        echo "${response}" >&2
        return 1
    }
}

EXPECTED_BROKER_STATS=(
    "kafka.kafka_broker.request.api_versions_request"
    "kafka.kafka_broker.request.metadata_request"
    "kafka.kafka_broker.request.create_topics_request"
    "kafka.kafka_broker.request.produce_request"
    "kafka.kafka_broker.request.fetch_request"
    "kafka.kafka_broker.response.api_versions_response"
    "kafka.kafka_broker.response.metadata_response"
    "kafka.kafka_broker.response.create_topics_response"
    "kafka.kafka_broker.response.produce_response"
    "kafka.kafka_broker.response.fetch_response")
for stat in "${EXPECTED_BROKER_STATS[@]}"; do
    has_metric_with_at_least_1 "${stat}"
done

run_log "Check admin kafka_service stats"
EXPECTED_BROKER_STATS=(
    "cluster.kafka_service.max_host_weight: 1"
    "cluster.kafka_service.membership_healthy: 1"
    "cluster.kafka_service.membership_total: 1")
for stat in "${EXPECTED_BROKER_STATS[@]}"; do
    filter="$(echo "$stat" | cut -d: -f1)"
    responds_with \
        "$stat" \
        "http://localhost:${PORT_ADMIN}/stats?filter=${filter}"
done
