#!/usr/bin/env python3
"""根据 db/schema.sql + db/seed.sql 生成 db/charge.db（在项目根目录运行）

用法:
    python3 tools/make_db.py
"""
import os
import sqlite3
import sys

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB_DIR = os.path.join(BASE, 'db')
DB_PATH = os.path.join(DB_DIR, 'charge.db')


def read_sql(filename):
    """尝试多种编码读取 SQL 文件，避免中文乱码"""
    path = os.path.join(DB_DIR, filename)
    for enc in ('utf-8', 'gbk', 'utf-8-sig'):
        try:
            with open(path, 'r', encoding=enc) as f:
                return f.read(), enc
        except (UnicodeDecodeError, UnicodeError):
            continue
    raise RuntimeError(f'cannot decode {filename}')


def main() -> int:
    if not os.path.isdir(DB_DIR):
        print(f'ERROR: 找不到 db 目录: {DB_DIR}', file=sys.stderr)
        return 1

    # 已存在的库会被覆盖重建（保证与 schema/seed 一致）
    if os.path.exists(DB_PATH):
        os.remove(DB_PATH)

    schema, enc1 = read_sql('schema.sql')
    seed, enc2 = read_sql('seed.sql')
    print(f'schema.sql encoding: {enc1}, seed.sql encoding: {enc2}')

    conn = sqlite3.connect(DB_PATH)
    conn.executescript(schema)
    conn.executescript(seed)
    conn.commit()

    cur = conn.cursor()
    for table in ('admin', 'user', 'station', 'pile', 'charge_order', 'wallet_log',
                  'favorite_station', 'operation_log', 'announcement',
                  'load_forecast', 'ads_daily_stats'):
        cur.execute(f'SELECT COUNT(*) FROM {table}')
        print(f'{table}: {cur.fetchone()[0]} rows')

    cur.execute('SELECT phone, balance, status FROM user ORDER BY id')
    for row in cur.fetchall():
        print('  user:', row)

    conn.close()
    print('OK, db size:', os.path.getsize(DB_PATH), 'bytes')
    return 0


if __name__ == '__main__':
    sys.exit(main())
