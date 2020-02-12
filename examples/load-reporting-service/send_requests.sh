#!/usr/bin/env bash

counter=1
while [ $counter -le 50 ]
do 
  # generate random Port number to send requests 
  port1=80
  port2=81
  range=$(($port2-$port1+1))
  PORT=$RANDOM
  let "PORT %= $range"
  PORT=$(($PORT+$port1))

  curl -v localhost:$PORT/service
  ((counter++))
done