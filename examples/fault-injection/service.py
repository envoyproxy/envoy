from flask import Flask
from flask import request
import socket
import os
import sys

app = Flask(__name__)

@app.route('/')
def hello():
    return ('Hello from python app behind Envoy!')

if __name__ == "__main__":
    app.run(host='0.0.0.0', port=8080, debug=True)
