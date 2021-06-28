import pymysql.cursors

connection = pymysql.connect(
    host='127.0.0.1',
    user='root',
    password='123',
    database='test',
    port=9001,
    cursorclass=pymysql.cursors.DictCursor)
