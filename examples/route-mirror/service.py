import os

from flask import Flask, request
from flask.helpers import send_from_directory

app = Flask(__name__)


@app.route(f"/service/{os.environ['SERVICE_NAME']}")
def get_service():
    print(f"Host: {request.headers.get('Host')}", flush=True)
    return f"Hello from behind Envoy (service {os.environ['SERVICE_NAME']})!\n"


if __name__ == "__main__":
    app.run(host='0.0.0.0', port=8080)
