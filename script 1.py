

#!/usr/bin/env python3
import paho.mqtt.client as mqtt
import requests
import json
import ssl
import base64
import mariadb
import struct
from influxdb_client import InfluxDBClient, Point
from influxdb_client.client.write_api import SYNCHRONOUS
from datetime import datetime
import sys
import signal

TELEGRAM_BOT_TOKEN = "8789839873:AAGZ-QhmI9hxGKaPDjgouZCXNuxJd6IgVg4"
TELEGRAM_CHAT_ID = "8646885195"

TTN_BROKER = "eu1.cloud.thethings.network"
TTN_PORT = 8883
TTN_USERNAME = "brandalarm@ttn"
TTN_PASSWORD = "NNSXS.6MKN4Z4YUGV3AOPXVA6DPGC6OCEAE6JUO7RHXPI.HO5NQOUSSTWV67XTCXSQTNTPRAZFG6BILTWYNQVHSF4ANRFLOHXQ"
TTN_APP_ID = "brandalarm@ttn"
TTN_TOPIC = f"v3/{TTN_APP_ID}/devices/+/up"

MARIADB_CONFIG = {
    'host': 'localhost',
    'user': 'yahya',
    'password': '',
    'database': 'alarm_system'
}

INFLUX_CONFIG = {
    'url': 'http://localhost:8086',
    'token': 'rZBAxvkYlM0tpW3EKIYyn2ZV--x2soRKcr4ca8IjfHv43UL_joyywIQcPlTB7xwUOxAwWwRXys_PgLF2eO4Jvw==',
    'org': 'sensor_system',
    'bucket': 'sensor_measurements'
}

# Label mapping fra SODAQ koden
LABELS = ["normal", "smoke", "heat", "gas", "motion"]

def send_telegram_message(message, is_alert=False):
    url = f"https://api.telegram.org/bot{TELEGRAM_BOT_TOKEN}/sendMessage"
    if is_alert:
        message = "🚨🚨🚨 **ALARM!** 🚨🚨🚨\n\n" + message
    payload = {"chat_id": TELEGRAM_CHAT_ID, "text": message, "parse_mode": "Markdown"}
    try:
        requests.post(url, json=payload, timeout=5)
        print("  📨 Telegram sent")
    except Exception as e:
        print(f"  ❌ Telegram error: {e}")

# I on_message funktionen, udskift besked opbygningen:

def decode_sodaq_payload(payload_raw_b64):
    """Decoder SODAQ Explore binær payload (14 bytes)"""
    try:
        payload_bytes = base64.b64decode(payload_raw_b64)
        print(f"     Hex: {payload_bytes.hex()}")
        print(f"     Length: {len(payload_bytes)} bytes")

        if len(payload_bytes) < 13:
            return {'error': f'Payload too short: {len(payload_bytes)} bytes'}

        # Decode efter SODAQ's struktur (big-endian)
        bmp280_raw = struct.unpack('>h', payload_bytes[0:2])[0]
        mcp9700_raw = struct.unpack('>h', payload_bytes[2:4])[0]
        mq9_raw = struct.unpack('>H', payload_bytes[4:6])[0]
        flame_raw = struct.unpack('>H', payload_bytes[6:8])[0]
        pir = payload_bytes[8]
        label_index = payload_bytes[9]
        confidence_raw = struct.unpack('>H', payload_bytes[10:12])[0]
        flags = payload_bytes[12] if len(payload_bytes) > 12 else 0

        # Konverter til rigtige værdier bmp280_temp = bmp280_raw / 100.0
        mcp9700_temp = mcp9700_raw / 100.0
        confidence = confidence_raw / 10000.0

        alarm = (flags & 0x01) != 0
        motion = (flags & 0x02) != 0

        label = LABELS[label_index] if label_index < len(LABELS) else f"unknown_{label_index}"

        data = {
            'bmp280': bmp280_temp,
            'mcp9700': mcp9700_temp,
            'mq9': mq9_raw,
            'flame': flame_raw,
            'pir': pir,
            'label': label,
            'label_index': label_index,
            'confidence': confidence,
            'alarm': alarm,
            'motion': motion
        }

        print(f"     BMP280: {bmp280_temp}°C")
        print(f"     MCP9700: {mcp9700_temp}°C")
        print(f"     MQ9: {mq9_raw}")
        print(f"     Flame: {flame_raw}")
        print(f"     PIR: {pir}")
        print(f"     Label: {label} ({confidence*100:.1f}%)")
        print(f"     Alarm: {alarm}, Motion: {motion}")

        return data

    except Exception as e:
        print(f"  Decode error: {e}")
        return {'error': str(e), 'raw': payload_raw_b64}

def insert_influxdb(data, dev_id, timestamp): 
 try:
        client = InfluxDBClient(url=INFLUX_CONFIG['url'], token=INFLUX_CONFIG['token'], org=INFLUX_CONFIG['org'])
        write_api = client.write_api(write_options=SYNCHRONOUS)
        point = Point("sensor_data") \
            .tag("device", dev_id) \
            .tag("location", "livingroom") \
            .tag("label", data.get('label', 'unknown')) \
            .tag("alarm_status", str(data.get('alarm', False)).lower()) \
            .field("bmp280_temp", float(data.get('bmp280', 0))) \
            .field("mcp9700_temp", float(data.get('mcp9700', 0))) \
            .field("mq9_gas", int(data.get('mq9', 0))) \
            .field("flame_value", int(data.get('flame', 0))) \
            .field("pir_motion", int(data.get('pir', 0))) \
            .field("confidence", float(data.get('confidence', 0))) \
            .field("alarm", 1 if data.get('alarm') else 0) \
            .field("motion_flag", 1 if data.get('motion') else 0) \
            .time(timestamp)
        write_api.write(bucket=INFLUX_CONFIG['bucket'], org=INFLUX_CONFIG['org'], record=point)
        client.close()
        print(f"  ✅ InfluxDB: {dev_id}")
    except Exception as e:
        print(f"  ❌ InfluxDB: {e}")

def insert_mariadb_alarm(data, dev_id):
    if not data.get('alarm', False):
        return False
    try:
        conn = mariadb.connect(**MARIADB_CONFIG)
        cursor = conn.cursor()
        cursor.execute("""
            INSERT INTO alarms (device_id, label, confidence, bmp280_temp, mcp9700_temp, mq9_gas, flame_value, pir_motion)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        """, (dev_id, data.get('label'), data.get('confidence'), data.get('bmp280'), data.get('mcp9700'), data.get('mq9'), data.get('flame'), data.get('pir')))
        conn.commit()
        conn.close()
        print(f"  🚨 MariaDB (alarms): ALARM gemt")
        return True
    except Exception as e: print(f"  ❌ MariaDB alarm error: {e}")
        return False

def insert_mariadb_statistics(data, dev_id):
    try:
        conn = mariadb.connect(**MARIADB_CONFIG)
        cursor = conn.cursor()
        cursor.execute("INSERT INTO statistics (device_id, total_measurements, total_alarms) VALUES (?, ?, ?)", 
                      (dev_id, 1, 1 if data.get('alarm') else 0))
        conn.commit()
        conn.close()
        print(f"  ✅ MariaDB (statistics): stats")
    except Exception as e:
        print(f"  ❌ MariaDB statistics error: {e}")

def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        print("✅ Connected to TTN")
        client.subscribe(TTN_TOPIC)
        print(f"📡 Subscribed to: {TTN_TOPIC}")
    else:
        print(f"❌ Connection failed: {rc}")

def on_message(client, userdata, msg):
    try:
        print(f"\n📨 Message received")
        payload = json.loads(msg.payload.decode('utf-8'))
        dev_id = payload.get('end_device_ids', {}).get('device_id', 'unknown')
        uplink = payload.get('uplink_message', {})
        frm_payload = uplink.get('frm_payload', '')
        rx_meta = uplink.get('rx_metadata', [{}])
        rssi = rx_meta[0].get('rssi', 0) if rx_meta else 0
        snr = rx_meta[0].get('snr', 0.0) if rx_meta else 0.0

        print(f"   Device: {dev_id}, RSSI: {rssi}, SNR: {snr}")

        sensor_data = decode_sodaq_payload(frm_payload)  if 'error' in sensor_data:
            print(f"   Decode error: {sensor_data['error']}")
            return

        sensor_data['rssi'] = rssi
        sensor_data['snr'] = snr

        timestamp = datetime.utcnow()
        insert_influxdb(sensor_data, dev_id, timestamp)
        insert_mariadb_alarm(sensor_data, dev_id)
        insert_mariadb_statistics(sensor_data, dev_id)

        # Byg Telegram besked
        alarm_emoji = "🚨" if sensor_data.get('alarm') else "✅"
        msg_text = f"{alarm_emoji} **{dev_id}**\n"
        msg_text += f"🌡️ **Temperatur:** {sensor_data.get('bmp280', 'N/A')}°C\n"
        msg_text += f"💨 **Gas (MQ-9):** {sensor_data.get('mq9', 'N/A')}\n"
        msg_text += f"🔥 **Flamme:** {sensor_data.get('flame', 'N/A')}\n"
        msg_text += f"🚶 **PIR motion:** {'JA' if sensor_data.get('pir') else 'NEJ'}\n"
        msg_text += f"🏷️ **Label:** {sensor_data.get('label', 'unknown')} ({sensor_data.get('confidence', 0)*100:.1f}%)\n"
        msg_text += f"📶 **Signal:** RSSI {rssi} dBm / SNR {snr} dB\n"
        msg_text += f"🕐 **Tid:** {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}"

        send_telegram_message(msg_text, is_alert=sensor_data.get('alarm', False))

    except Exception as e:
        print(f"❌ Error: {e}")
        import traceback
        traceback.print_exc()

def on_disconnect(client, userdata, rc, properties=None):
    print("🔌 Disconnected from TTN")

def main():
    print("=" * 60)
    print("🔥 TTN MQTT Listener - SODAQ Explore (Binær decoder) 🔥")
    print("=" * 60)  print(f"Server: {TTN_BROKER}:{TTN_PORT} (TLS)")
    print(f"App ID: {TTN_APP_ID}")
    print("=" * 60)
    
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="ttn_listener")
    client.on_connect = on_connect
    client.on_message = on_message
    client.on_disconnect = on_disconnect
    client.username_pw_set(TTN_USERNAME, TTN_PASSWORD)
    client.tls_set(ca_certs="/etc/ssl/certs/ca-certificates.crt", cert_reqs=ssl.CERT_REQUIRED)
    
    try:
        client.connect(TTN_BROKER, TTN_PORT, 60)
    except Exception as e:
        print(f"❌ Connect failed: {e}")
        sys.exit(1)
    
    print("\n🎧 Listening for messages... (Ctrl+C to stop)\n")
    
    def signal_handler(sig, frame):
        print("\n👋 Stopping...")
        client.disconnect()
        sys.exit(0)
    
    signal.signal(signal.SIGINT, signal_handler)
    client.loop_forever()

if __name__ == "__main__":
    main()




