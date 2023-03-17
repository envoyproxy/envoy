#!/usr/bin/env bash

PORT_PROXY0=${PORT_PROXY0:-80}
PORT_PROXY1=${PORT_PROXY1:-81}

counter=1
while [ $counter -le 50 ]
do
  # generate random Port number to send requests
  ports=("${PORT_PROXY0}" "${PORT_PROXY1}")
  port=${ports[$RANDOM % ${#ports[@]} ]}

  curl -v "localhost:${port}/service"
  ((counter++))
done
