
import sqlite3
import os
from datetime import datetime

DB_FILE = "iot_data.db"

def get_connection():
    """获取数据库连接"""
    conn = sqlite3.connect(DB_FILE)
    conn.row_factory = sqlite3.Row  # 让查询结果可以按列名访问
    return conn

def init_db():
    """初始化数据库表结构"""
    conn = get_connection()
    cursor = conn.cursor()
    
    # 设备表
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS devices (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            device_id TEXT UNIQUE NOT NULL,
            first_seen TEXT,
            last_seen TEXT
        )
    ''')
    
    # 传感器数据表
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS sensor_data (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            device_id TEXT NOT NULL,
            temperature REAL,
            humidity REAL,
            received_at TEXT NOT NULL
        )
    ''')
    
    # 索引（加速查询）
    cursor.execute('''
        CREATE INDEX IF NOT EXISTS idx_device_time 
        ON sensor_data(device_id, received_at)
    ''')
    
    conn.commit()
    conn.close()
    print("Database initialized")

def insert_sensor_data(device_id, temperature, humidity):
    """插入传感器数据"""
    conn = get_connection()
    cursor = conn.cursor()
    
    now = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    
    # 插入数据
    cursor.execute('''
        INSERT INTO sensor_data (device_id, temperature, humidity, received_at)
        VALUES (?, ?, ?, ?)
    ''', (device_id, temperature, humidity, now))
    
    # 更新或插入设备信息
    cursor.execute('''
        INSERT INTO devices (device_id, first_seen, last_seen)
        VALUES (?, ?, ?)
        ON CONFLICT(device_id) DO UPDATE SET last_seen = ?
    ''', (device_id, now, now, now))
    
    conn.commit()
    row_id = cursor.lastrowid
    conn.close()
    return row_id

def get_all_data(limit=1000):
    """获取所有数据（默认最多 1000 条）"""
    conn = get_connection()
    cursor = conn.cursor()
    cursor.execute('''
        SELECT device_id, temperature, humidity, received_at
        FROM sensor_data
        ORDER BY id DESC
        LIMIT ?
    ''', (limit,))
    rows = cursor.fetchall()
    conn.close()
    # 反转顺序，让最新的在最后（方便画图）
    return [dict(row) for row in reversed(rows)]

def get_latest_data():
    """获取最新一条数据"""
    conn = get_connection()
    cursor = conn.cursor()
    cursor.execute('''
        SELECT device_id, temperature, humidity, received_at
        FROM sensor_data
        ORDER BY id DESC
        LIMIT 1
    ''')
    row = cursor.fetchone()
    conn.close()
    return dict(row) if row else None

def get_total_count():
    """获取数据总条数"""
    conn = get_connection()
    cursor = conn.cursor()
    cursor.execute('SELECT COUNT(*) FROM sensor_data')
    count = cursor.fetchone()[0]
    conn.close()
    return count

def get_devices():
    """获取所有设备信息"""
    conn = get_connection()
    cursor = conn.cursor()
    cursor.execute('SELECT device_id, first_seen, last_seen FROM devices ORDER BY last_seen DESC')
    rows = cursor.fetchall()
    conn.close()
    return [dict(row) for row in rows]

def migrate_from_json(json_file="data.json"):
    """从旧 JSON 文件迁移数据"""
    import json
    if not os.path.exists(json_file):
        print(f"No {json_file} to migrate")
        return 0
    
    with open(json_file, 'r') as f:
        records = json.load(f)
    
    if not records:
        print("No records to migrate")
        return 0
    
    conn = get_connection()
    cursor = conn.cursor()
    count = 0
    
    for r in records:
        device_id = r.get('device_id', 'unknown')
        temperature = r.get('temperature') or r.get('temprature')  # 兼容拼写错误
        humidity = r.get('humidity')
        received_at = r.get('received_at', datetime.now().strftime('%Y-%m-%d %H:%M:%S'))
        
        cursor.execute('''
            INSERT INTO sensor_data (device_id, temperature, humidity, received_at)
            VALUES (?, ?, ?, ?)
        ''', (device_id, temperature, humidity, received_at))
        
        cursor.execute('''
            INSERT INTO devices (device_id, first_seen, last_seen)
            VALUES (?, ?, ?)
            ON CONFLICT(device_id) DO UPDATE SET last_seen = ?
        ''', (device_id, received_at, received_at, received_at))
        
        count += 1
    
    conn.commit()
    conn.close()
    print(f"Migrated {count} records from {json_file}")
    return count

if __name__ == '__main__':
    init_db()
    migrate_from_json()
