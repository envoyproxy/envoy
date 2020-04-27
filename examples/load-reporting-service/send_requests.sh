#!/usr/bin/env bash

counter=1
while [ $counter -le 50 ]
do 
  # generate random Port number to send requests
  ports=("80" "81")
  port=${ports[$RANDOM % ${#ports[@]} ]}

  curl -v localhost:$port/service
  ((counter++))
done