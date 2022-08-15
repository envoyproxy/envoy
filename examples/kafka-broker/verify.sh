#!/bin/bash -e

export NAME=kafka-broker
export PORT_PROXY="${KAFKA_PORT_PROXY:-11100}"
export PORT_ADMIN="${KAFKA_PORT_ADMIN:-11101}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

# Initialize the topic
TOPIC="envoy-kafka-broker"

# Initialize the message for the producer
MESSAGE="Welcome to Envoy and Kafka Broker filter!"

run_log "Create a Kafka topic"
docker-compose exec -T kafka-server kafka-topics --bootstrap-server localhost:19092 --create --topic $TOPIC quickstart-events

run_log "Check the Kafka topic"
docker-compose exec -T kafka-server kafka-topics --bootstrap-server localhost:19092 --list | grep $TOPIC

# Producer
run_log "Send a message using the Kafka producer"
docker-compose exec -T kafka-server /bin/bash -c " \
    echo $MESSAGE >> message.txt \
    && kafka-console-producer --request-required-acks 1 --broker-list localhost:19092 --topic $TOPIC < message.txt"

# Consumer
run_log "Receive a message using the Kafka consumer"
docker-compose exec -T kafka-server kafka-console-consumer --bootstrap-server localhost:19092 --topic $TOPIC --from-beginning --max-messages 1
