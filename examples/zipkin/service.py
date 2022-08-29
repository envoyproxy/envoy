import os

from flask import Flask
from flask.helpers import send_from_directory

app = Flask(__name__)


@app.route(f"/trace/{os.environ['SERVICE_NAME']}")
def get_service():
    return f"Hello from behind Envoy (service {os.environ['SERVICE_NAME']})!\n"


if __name__ == "__main__":
    app.run(host='0.0.0.0', port=8080)
