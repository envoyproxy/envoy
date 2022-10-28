from flask import Flask, request, Response
from flask.helpers import send_from_directory

app = Flask(__name__)


@app.route('/file.txt')
def get_plain_file():
    return send_from_directory("data", "file.txt")


@app.route('/file.json')
def get_json_file():
    return send_from_directory("data", "file.json")


@app.route("/upload", methods=['POST'])
def test_decompressor():
    resp = Response("OK")
    resp.headers["decompressed-size"] = len(next(iter(request.form)))
    return resp


if __name__ == "__main__":
    app.run(host='0.0.0.0', port=8080)
