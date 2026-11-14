from flask import Flask, request, jsonify, render_template
import json
import os
from datetime import datetime

app = Flask(__name__)

DATA_FILE = "data.json"

# 确保数据文件存在
if not os.path.exists(DATA_FILE):
    with open(DATA_FILE, 'w') as f:
        json.dump([], f)

@app.route('/api/data', methods=['POST'])
def receive_data():
    """接收网关转发的数据"""
    try:
        data = request.get_json()

        if data is None:
            return jsonify({"status": "error", "msg": "Invalid JSON"}), 400

        # 添加接收时间
        data['received_at'] = datetime.now().strftime('%Y-%m-%d %H:%M:%S')

        # 读取现有数据
        with open(DATA_FILE, 'r') as f:
            records = json.load(f)

        # 追加新数据
        records.append(data)

        # 写回文件
        with open(DATA_FILE, 'w') as f:
            json.dump(records, f, indent=2, ensure_ascii=False)

        print(f"[{data['received_at']}] Received: {data}")

        return jsonify({"status": "ok", "total": len(records)})

    except Exception as e:
        # 打印完整错误到控制台，方便调试
        import traceback
        traceback.print_exc()
        return jsonify({"status": "error", "msg": str(e)}), 500

@app.route('/api/data', methods=['GET'])
def get_data():
    """查看所有数据"""
    with open(DATA_FILE, 'r') as f:
        records = json.load(f)
    return jsonify(records)

@app.route('/api/latest', methods=['GET'])
def get_latest():
    """查看最新一条数据"""
    with open(DATA_FILE, 'r') as f:
        records = json.load(f)
    if not records:
        return jsonify({"msg": "No data"}), 404
    return jsonify(records[-1])

@app.route('/health', methods=['GET'])
def health():
    """健康检查"""
    return jsonify({"status": "healthy", "service": "cloud-api"})

@app.route('/dashboard')
def dashboard():
    return render_template('dashboard.html')

if __name__ == '__main__':
    print("Cloud API starting on port 5000...")
    app.run(host='0.0.0.0', port=5000, debug=False)

