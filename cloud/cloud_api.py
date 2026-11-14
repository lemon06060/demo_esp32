from flask import Flask, request, jsonify, render_template
from datetime import datetime
import database

app = Flask(__name__)

# 启动时初始化数据库
database.init_db()

# ============================================================
#  API 路由
# ============================================================
@app.route('/api/data', methods=['POST'])
def receive_data():
    try:
        data = request.get_json()
        if data is None:
            return jsonify({"status": "error", "msg": "Invalid JSON"}), 400
        
        device_id = data.get('device_id', 'unknown')
        temperature = data.get('temperature') or data.get('temprature')  # 兼容拼写
        humidity = data.get('humidity')
        
        # 写入数据库
        row_id = database.insert_sensor_data(device_id, temperature, humidity)
        total = database.get_total_count()
        
        print(f"[{datetime.now().strftime('%H:%M:%S')}] "
              f"Device={device_id}, Temp={temperature}, Humi={humidity}, ID={row_id}")
        
        return jsonify({"status": "ok", "id": row_id, "total": total})
    
    except Exception as e:
        import traceback
        traceback.print_exc()
        return jsonify({"status": "error", "msg": str(e)}), 500

@app.route('/api/data', methods=['GET'])
def get_data():
    limit = request.args.get('limit', 1000, type=int)
    data = database.get_all_data(limit)
    return jsonify(data)

@app.route('/api/latest', methods=['GET'])
def get_latest():
    data = database.get_latest_data()
    if not data:
        return jsonify({"msg": "No data"}), 404
    return jsonify(data)

@app.route('/api/devices', methods=['GET'])
def get_devices():
    return jsonify(database.get_devices())

@app.route('/api/stats', methods=['GET'])
def get_stats():
    """统计信息"""
    return jsonify({
        "total_records": database.get_total_count(),
        "devices": database.get_devices()
    })

@app.route('/health', methods=['GET'])
def health():
    return jsonify({"status": "healthy"})

# ============================================================
#  仪表盘页面
# ============================================================
@app.route('/dashboard')
def dashboard():
    return render_template('dashboard.html')

if __name__ == '__main__':
    print("Cloud API starting on port 5000...")
    app.run(host='0.0.0.0', port=5000, debug=False)
