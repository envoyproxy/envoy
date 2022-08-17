#!/bin/bash -e

export NAME=kafka-broker
export PORT_PROXY="${KAFKA_PORT_PROXY:-11100}"
export PORT_ADMIN="${KAFKA_PORT_ADMIN:-11101}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

kafka_client () {
    docker-compose run kafka-client "$@"
}

TOPIC="envoy-kafka-broker"

MESSAGE="Welcome to Envoy and Kafka Broker filter!"

run_log "Create a Kafka topic"
kafka_client kafka-topics --bootstrap-server proxy:10000 --create --topic $TOPIC quickstart-events

run_log "Check the Kafka topic"
kafka_client kafka-topics --bootstrap-server proxy:10000 --list | grep $TOPIC

run_log "Send a message using the Kafka producer"
kafka_client /bin/bash -c " \
    echo $MESSAGE >> message.txt \
    && kafka-console-producer --request-required-acks 1 --broker-list proxy:10000 --topic $TOPIC < message.txt"

run_log "Receive a message using the Kafka consumer"
kafka_client kafka-console-consumer --bootstrap-server proxy:10000 --topic $TOPIC --from-beginning --max-messages 1
