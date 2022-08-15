#!/bin/bash -e

export NAME=kafka-broker
export PORT_PROXY="${KAFKA_PORT_PROXY:-11100}"
export PORT_ADMIN="${KAFKA_PORT_ADMIN:-11101}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

# Initialize the topic
TOPIC="envoy-kafka-broker"

run_log "Create a Kafka topic"
docker-compose exec -T kafka kafka-topics --bootstrap-server localhost:19092 --create --topic $TOPIC quickstart-events

run_log "Check the Kafka topic"
docker-compose exec -T kafka kafka-topics --bootstrap-server localhost:19092 --list | grep $TOPIC

# Initialize message for producer
MESSAGE="Welcome to Envoy and Kafka Broker filter!"

# Producer
run_log "Create a producer and send the message"
docker-compose exec -T kafka /bin/bash -c "echo $MESSAGE >> message.txt & kafka-console-producer --request-required-acks 1 --broker-list localhost:19092 --topic $TOPIC < message.txt"
run_log "Sent messages succesfully"

# Consumer
run_log "Create a consumer"

read_message() {
    run_log "Reading the message"
    docker-compose exec -T kafka kafka-console-consumer --bootstrap-server localhost:19092 --topic $TOPIC --from-beginning >> consumer.txt
}

check_message() {
    while true; do
    if [[ $(< consumer.txt) == "$MESSAGE" ]]; then
        run_log "Received the message succesfully"
        rm consumer.txt # clean up consumer.txt
        run_log "Bring down the kafka"
        docker-compose stop kafka
        wait_for 10 sh -c "docker-compose ps kafka | grep -v unhealthy"
        run_log "Kafka was down"
        break
    else
        run_log "Checking the message"
        sleep 1
    fi;
    done
}

read_message &
check_message &
wait
