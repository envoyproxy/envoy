from flask import Flask
import os
import requests
import socket
import sys

app = Flask(__name__)


@app.route('/service/<service_number>')
def hello(service_number):
    return (
        'Hello from behind Envoy (service {})! hostname: {} resolved'
        'hostname: {}\n'.format(
            os.environ['SERVICE_NAME'], socket.gethostname(),
            socket.gethostbyname(socket.gethostname())))


@app.route('/trace/<service_number>')
def trace(service_number):
    if int(os.environ['SERVICE_NAME']) == 1:
        requests.get("http://localhost:9000/trace/2")
    return (
        'Hello from behind Envoy (service {})! hostname: {} resolved'
        'hostname: {}\n'.format(
            os.environ['SERVICE_NAME'], socket.gethostname(),
            socket.gethostbyname(socket.gethostname())))


if __name__ == "__main__":
    app.run(host='127.0.0.1', port=8080, debug=True)
