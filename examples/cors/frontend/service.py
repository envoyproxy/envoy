from flask import Flask, send_from_directory
import os

app = Flask(__name__)


@app.route('/')
def index():
    file_dir = os.path.dirname(os.path.realpath(__file__))
    return send_from_directory(file_dir, 'index.html')


if __name__ == "__main__":
    app.run(host='0.0.0.0', port=8000)
