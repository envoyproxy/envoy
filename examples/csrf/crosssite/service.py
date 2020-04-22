import os

from flask import Flask, send_from_directory

app = Flask(__name__)
app.url_map.strict_slashes = False


@app.route('/', methods=['GET'])
def index():
  file_dir = os.path.dirname(os.path.realpath(__file__))
  return send_from_directory(file_dir, 'index.html')


if __name__ == "__main__":
  app.run(host='127.0.0.1', port=8080, debug=True)
