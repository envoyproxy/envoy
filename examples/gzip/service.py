from flask import Flask
from flask.helpers import send_file
from flask_compress import Compress

app = Flask(__name__)
app.config["COMPRESS_ALGORITHM"] = 'gzip'
Compress(app)


@app.route('/plain')
def get_plain_file():
    return send_file('data/file', conditional=True)


@app.route('/json')
def get_json_file():
    return send_file('data/file.json', conditional=True)


if __name__ == "__main__":
    app.run(host='0.0.0.0', port=8080, debug=True)
