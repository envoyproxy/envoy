import sys

from flask import Flask

import psycopg2

app = Flask(__name__)


@app.route('/')
def hello():
    conn = psycopg2.connect("host=postgres user=postgres")
    cur = conn.cursor()
    cur.execute('SELECT version()')
    msg = 'Connected to Postgres, version: %s' % cur.fetchone()
    cur.close()
    return msg


if __name__ == "__main__":
    app.run(host='0.0.0.0', port=8000)
