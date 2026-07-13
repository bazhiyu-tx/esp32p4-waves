"""
PC → ESP32-P4 WiFi 音频流发送器

捕获电脑扬声器输出，通过 UDP 发送到 ESP32-P4。

用法：
    python pc_streamer.py                              # 自动发现 ESP32
    python pc_streamer.py 192.168.x.x                  # 手动指定 IP
    python pc_streamer.py --list-devices               # 列出音频设备

依赖：
    pip install sounddevice
"""

import socket
import struct
import sys
import time
import threading
import argparse

try:
    import sounddevice as sd
except ImportError:
    print("❌ 请先安装: pip install sounddevice")
    sys.exit(1)

# ── 配置 ────────────────────────────────────────────
ESP32_PORT = 5000
SAMPLE_RATE = 44100
CHANNELS = 1          # mono
BLOCK_SIZE = 512      # 每包采样数（512 × 2字节 = 1024字节/包）
DURATION_SEC = 5      # 每次缓冲发送的秒数，设为 0 持续发送
# ───────────────────────────────────────────────────

def discover_esp32():
    """通过 mDNS 自动发现 ESP32-P4"""
    try:
        from zeroconf import Zeroconf, ServiceBrowser, ServiceStateChange

        discovered = []

        def on_service(zeroconf, service_type, name, state_change):
            if state_change == ServiceStateChange.Added:
                info = zeroconf.get_service_info(service_type, name)
                if info:
                    for addr in info.parsed_addresses():
                        discovered.append(addr)
                        print(f"  ✅ 发现: {name} → {addr}")

        print("🔍 正在搜索 ESP32-P4 Audio (mDNS)...")
        zeroconf = Zeroconf()
        browser = ServiceBrowser(zeroconf, "_audio-stream._udp.local.", [on_service])
        time.sleep(3)
        zeroconf.close()

        if discovered:
            return discovered[0]
    except ImportError:
        # 没有 zeroconf，尝试直接解析主机名
        try:
            ip = socket.gethostbyname("esp32p4-audio.local")
            print(f"  ✅ 通过主机名解析到: {ip}")
            return ip
        except:
            pass
    return None

def list_devices():
    """列出所有音频设备（含 loopback）"""
    print("📢 可用的音频输入设备（含 WASAI loopback）:")
    print(sd.query_devices())
    print("\n💡 提示: 带 'loopback' 或 'Output' 的设备可以捕获系统声音")
    sys.exit(0)

def find_loopback_device():
    """自动找到 WASAPI loopback 设备（捕获扬声器输出）"""
    devices = sd.query_devices()
    for i, dev in enumerate(devices):
        name = dev['name'].lower()
        # 优先找 loopback 设备（WASAPI 独占）
        if 'loopback' in name and dev['max_input_channels'] > 0:
            print(f"  ✅ 使用 loopback 设备: [{i}] {dev['name']}")
            return i
    # 其次找默认的立体声混音或输出设备
    for i, dev in enumerate(devices):
        if dev['max_input_channels'] > 0 and 'output' in name:
            print(f"  ⚠️ 使用输出设备: [{i}] {dev['name']}")
            return i
    # 最后用默认输入设备
    default = sd.default.device[0]
    print(f"  ⚠️ 使用默认输入设备: [{default}] {devices[default]['name']}")
    return default

def main():
    parser = argparse.ArgumentParser(description="PC → ESP32-P4 音频流发送器")
    parser.add_argument('ip', nargs='?', help='ESP32 IP 地址')
    parser.add_argument('--list-devices', action='store_true', help='列出音频设备')
    parser.add_argument('--device', type=int, default=None, help='音频输入设备索引')
    args = parser.parse_args()

    if args.list_devices:
        list_devices()

    if args.ip:
        esp32_ip = args.ip
        print(f"📡 手动指定目标: {esp32_ip}")
    else:
        esp32_ip = discover_esp32()
        if not esp32_ip:
            print("❌ 未自动发现设备")
            print("用法: python pc_streamer.py <ESP32_IP>")
            sys.exit(1)

    print(f"🎯 目标 ESP32: {esp32_ip}:{ESP32_PORT}")

    # ── 音频设备 ──
    if args.device is not None:
        loopback_dev = args.device
        print(f"  🎤 使用指定设备: [{loopback_dev}]")
    else:
        loopback_dev = find_loopback_device()

    # ── UDP Socket ──
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    seq = 0
    running = True

    def audio_callback(indata, frames, time_info, status):
        """音频数据回调（sounddevice 自动调用）"""
        nonlocal seq
        if status:
            print(f"⚠️ {status}")
        # 包装: [4字节序号] + [PCM数据]
        pkt = struct.pack('I', seq) + indata.tobytes()
        try:
            sock.sendto(pkt, (esp32_ip, ESP32_PORT))
        except Exception as e:
            print(f"❌ 发送失败: {e}")
        seq += 1

    # ── 启动流 ──
    print(f"🎤 正在捕获系统音频 ({SAMPLE_RATE}Hz, {CHANNELS}ch)...")
    print("  按 Ctrl+C 停止\n")

    try:
        with sd.InputStream(
            device=loopback_dev,
            samplerate=SAMPLE_RATE,
            channels=CHANNELS,
            blocksize=BLOCK_SIZE,
            dtype='int16',
            callback=audio_callback
        ):
            while running:
                time.sleep(0.1)
    except KeyboardInterrupt:
        print("\n⏹ 停止发送")
    except Exception as e:
        print(f"❌ 音频错误: {e}")
        print("\n💡 提示:")
        print("  - 检查音频设备是否支持 loopback 捕获")
        print("  - 用 --list-devices 查看可用设备")
        print("  - 可以指定设备索引: python pc_streamer.py <IP> --device <ID>")
    finally:
        sock.close()
        print("👋 已退出")

if __name__ == '__main__':
    main()
