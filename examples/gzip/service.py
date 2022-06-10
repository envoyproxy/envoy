from flask import Flask
from flask.helpers import send_from_directory

app = Flask(__name__)


@app.route('/file.txt')
def get_plain_file():
    return send_from_directory("data", "file.txt")


@app.route('/file.json')
def get_json_file():
    return send_from_directory("data", "file.json")


if __name__ == "__main__":
    app.run(host='0.0.0.0', port=8080)
