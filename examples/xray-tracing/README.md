## README

* Build sample app
  - inside current directory run command
  ````
  docker-compose up --build -d
  ````
  - generate traffic to envoy endpoint
  
  ``curl http://127.0.0.1:8000/trace/1``
  - log into console to look at the trace and service map


