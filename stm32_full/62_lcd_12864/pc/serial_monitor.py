#!/usr/bin/env python3
"""
STM32 + 12864B LCD 串口信息显示器
===================================

通过串口向 STM32 发送系统信息或自定义文字，
STM32 接收后驱动 12864B (ST7920) LCD 显示。

功能模式:
  1. 系统监控模式 (默认): 显示 CPU、内存、磁盘等信息
  2. 自定义文字模式 (-t): 显示自定义 4 行文字
  3. 位图/图片模式 (-i): 将图片转换为 128x64 单色位图显示

用法:
  python serial_monitor.py                          # 系统监控模式
  python serial_monitor.py -p COM3                  # 指定串口
  python serial_monitor.py -t "CPU:50%" "MEM:2GB"   # 自定义文字
  python serial_monitor.py -i image.png             # 显示图片
  python serial_monitor.py -b 115200                # 指定波特率

依赖:
  pip install pyserial psutil Pillow
"""

import argparse
import os
import sys
import time
try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("❌ 缺少 pyserial 库，请运行: pip install pyserial")
    sys.exit(1)

try:
    import psutil
    HAS_PSUTIL = True
except ImportError:
    HAS_PSUTIL = False

try:
    from PIL import Image
    HAS_PIL = True
except ImportError:
    HAS_PIL = False


# ============================================================
# 协议常量 (与 STM32 固件保持一致)
# ============================================================
FRAME_HEADER     = 0xAA
FRAME_MODE_TEXT  = 0x01
FRAME_MODE_BITMAP = 0x02
FRAME_ACK        = 0x55
LCD_LINE_WIDTH   = 16
LCD_LINE_COUNT   = 4
TEXT_DATA_SIZE   = LCD_LINE_WIDTH * LCD_LINE_COUNT  # 64 bytes
BITMAP_DATA_SIZE = 1024  # 128 * 64 / 8


# ============================================================
# 串口查找
# ============================================================
def find_serial_port():
    """自动查找可用的串口"""
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        return None

    print("📡 可用串口:")
    for i, port in enumerate(ports):
        info = f"  [{i}] {port.device} - {port.description}"
        if port.manufacturer:
            info += f" ({port.manufacturer})"
        print(info)

    # 如果只有一个，自动选择
    if len(ports) == 1:
        print(f"✅ 自动选择: {ports[0].device}")
        return ports[0].device

    # 多个时优先选常见型号
    for p in ports:
        lower_desc = (p.description + str(p.manufacturer)).lower()
        if any(kw in lower_desc for kw in ['ch340', 'cp210', 'stlink', 'ftdi', 'usb-serial']):
            print(f"✅ 自动选择: {p.device} ({p.description})")
            return p.device

    # 否则让用户选
    while True:
        try:
            choice = input("选择串口编号: ").strip()
            idx = int(choice)
            if 0 <= idx < len(ports):
                return ports[idx].device
        except (ValueError, IndexError):
            pass
        print("请输入有效的编号")


def open_serial(port, baudrate=921600):
    """打开串口连接"""
    if port is None:
        port = find_serial_port()
        if port is None:
            print("❌ 未找到串口设备")
            print("   - 检查 USB-TTL 是否已插入")
            print("   - Linux: 检查 /dev/ttyUSB* 或 /dev/ttyACM*")
            print("   - 手动指定: python serial_monitor.py -p /dev/ttyUSB0")
            sys.exit(1)

    print(f"🔌 连接串口: {port} @ {baudrate} baud")
    try:
        ser = serial.Serial(
            port=port,
            baudrate=baudrate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=1.0,
            write_timeout=1.0
        )
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        return ser
    except serial.SerialException as e:
        print(f"❌ 打开串口失败: {e}")
        sys.exit(1)


def wait_frame_ack(ser):
    """等待 STM32 处理完当前帧并准备接收下一帧。"""
    ack = ser.read(1)
    if ack != bytes([FRAME_ACK]):
        raise serial.SerialTimeoutException(
            f"等待 STM32 ACK 超时或收到错误字节: {ack!r}"
        )


# ============================================================
# 文本帧构造与发送
# ============================================================
def make_text_frame(lines):
    """
    构造 66 字节文本帧
    lines: 4 个字符串的列表，每个最多 16 字符

    帧格式: [0xAA] [0x01] [64 bytes text data]
    """
    frame = bytearray()
    frame.append(FRAME_HEADER)
    frame.append(FRAME_MODE_TEXT)

    for i in range(LCD_LINE_COUNT):
        if i < len(lines):
            text = str(lines[i])
        else:
            text = ""

        # 截断或补空格到 16 字符
        if len(text) > LCD_LINE_WIDTH:
            text = text[:LCD_LINE_WIDTH]
        text = text.ljust(LCD_LINE_WIDTH)

        frame.extend(text.encode('ascii', errors='replace'))

    return bytes(frame)


def send_text_frame(ser, lines):
    """发送文本帧"""
    frame = make_text_frame(lines)
    ser.write(frame)
    ser.flush()
    wait_frame_ack(ser)


# ============================================================
# 位图帧构造与发送
# ============================================================
def image_to_bitmap(image_path, invert=False):
    """
    将图片转换为 128x64 单色位图 (1024 bytes)

    位图格式:
      - 128 列 × 64 行
      - 每字节 8 个水平像素 (MSB=最左)
      - 字节布局: byte[y*16 + x], y 行 x 列 (每列 8 像素)
    """
    if not HAS_PIL:
        print("❌ 缺少 Pillow 库，请运行: pip install Pillow")
        sys.exit(1)

    img = Image.open(image_path).convert('L')  # 转灰度
    img = img.resize((128, 64), Image.LANCZOS)

    # 二值化 (阈值 128)
    pixels = img.load()
    bitmap = bytearray(BITMAP_DATA_SIZE)

    for y in range(64):
        for x_byte in range(16):  # 16 bytes per row
            byte_val = 0
            for bit in range(8):
                px = pixels[x_byte * 8 + bit, y]
                # px=0 黑色, px=255 白色
                bit_val = 1 if (px < 128) != invert else 0
                byte_val |= (bit_val << (7 - bit))  # MSB first
            bitmap[y * 16 + x_byte] = byte_val

    return bytes(bitmap)


def make_bitmap_frame(bitmap_data):
    """
    构造 1026 字节日图帧

    帧格式: [0xAA] [0x02] [1024 bytes bitmap data]
    """
    frame = bytearray()
    frame.append(FRAME_HEADER)
    frame.append(FRAME_MODE_BITMAP)
    frame.extend(bitmap_data)
    return bytes(frame)


def send_bitmap_frame(ser, bitmap_data):
    """发送位图帧"""
    frame = make_bitmap_frame(bitmap_data)
    # 分批发送 (避免一次发送太多导致缓冲区溢出)
    CHUNK = 256
    for i in range(0, len(frame), CHUNK):
        ser.write(frame[i:i+CHUNK])
    ser.flush()
    wait_frame_ack(ser)


# ============================================================
# 系统信息获取
# ============================================================
def get_system_info():
    """获取系统状态信息"""
    lines = []

    if HAS_PSUTIL:
        # Line 1: CPU 使用率
        cpu_percent = psutil.cpu_percent(interval=0.1)
        cpu_bar = make_bar(int(cpu_percent), 10)
        lines.append(f"CPU{cpu_bar}{cpu_percent:>5.1f}%")

        # Line 2: 内存使用
        mem = psutil.virtual_memory()
        mem_used_gb = mem.used / (1024**3)
        mem_total_gb = mem.total / (1024**3)
        mem_bar = make_bar(int(mem.percent), 10)
        lines.append(f"MEM{mem_bar}{mem.percent:>5.1f}%")

        # Line 3: 磁盘 /
        try:
            disk = psutil.disk_usage('/')
            disk_bar = make_bar(int(disk.percent), 10)
            lines.append(f"DISK{disk_bar}{disk.percent:>5.1f}%")
        except Exception:
            lines.append("DISK ---.--% --")

        # Line 4: 网络 / 时间
        try:
            net = psutil.net_io_counters()
            sent_kb = net.bytes_sent / 1024
            recv_kb = net.bytes_recv / 1024
            lines.append(f"NET {sent_kb:>4.0f}↑{recv_kb:>4.0f}↓KB")
        except Exception:
            lines.append(time.strftime("TIME %H:%M:%S    "))
    else:
        # 无 psutil 时的后备显示
        lines.append(" Install psutil  ")
        lines.append(" pip install     ")
        lines.append(" psutil          ")
        lines.append(time.strftime(" %H:%M:%S %y-%m-%d"))

    return lines


def make_bar(percent, width):
    """生成进度条字符串 (用 █ ▌ 字符)"""
    filled = int(percent / 100.0 * width)
    # 用 ASCII 字符代替 Unicode, ST7920 CGROM 只支持 ASCII
    bar = '#' * filled + '-' * (width - filled)
    return bar


# ============================================================
# 主循环
# ============================================================
def run_system_monitor(ser, interval=0.5):
    """系统监控模式: 持续发送 CPU/内存信息"""
    print(f"\n📊 系统监控模式 (刷新间隔: {interval}s)")
    print("   按 Ctrl+C 退出\n")

    try:
        while True:
            lines = get_system_info()
            # 从左截断到 16 字符
            lines_16 = [l[:LCD_LINE_WIDTH] for l in lines]

            # 打印本地显示
            print("\033[F" * 5, end="")  # 上移 5 行
            print("┌──────────────────┐")
            for l in lines_16:
                print(f"│{l}│")
            print("└──────────────────┘")

            send_text_frame(ser, lines_16)
            time.sleep(interval)

    except KeyboardInterrupt:
        print("\n\n👋 退出系统监控模式")


def run_custom_text(ser, lines):
    """自定义文字模式: 发送一次后保持"""
    # 确保有 4 行
    while len(lines) < LCD_LINE_COUNT:
        lines.append("")

    print(f"\n📝 发送自定义文字:")
    print("┌──────────────────┐")
    for l in lines[:4]:
        print(f"│{l[:LCD_LINE_WIDTH].ljust(LCD_LINE_WIDTH)}│")
    print("└──────────────────┘")

    try:
        print("\n   持续发送中，按 Ctrl+C 退出")
        while True:
            send_text_frame(ser, lines[:4])
            time.sleep(2.0)  # 定期刷新
    except KeyboardInterrupt:
        print("\n👋 退出")


def run_image_mode(ser, image_path, invert=False):
    """图片模式: 将图片转为位图发送"""
    print(f"\n🖼️  处理图片: {image_path}")
    bitmap = image_to_bitmap(image_path, invert)
    print(f"   位图大小: {len(bitmap)} bytes")

    print("   发送位图到 LCD...")
    send_bitmap_frame(ser, bitmap)
    print("✅ 发送完成")


def run_interactive(ser):
    """交互模式: 在终端输入文字, 实时发送到 LCD"""
    print(f"\n💬 交互模式")
    print("   输入 4 行文字 (每行最多 16 字符), 空行保持原内容")
    print("   输入 'q' 退出, 'c' 清屏\n")

    current_lines = ["Hello from PC!  ", "STM32 + 12864B   ", "Interactive Mode ", "Type text below  "]

    try:
        while True:
            print("┌──────────────────┐")
            for i, l in enumerate(current_lines):
                print(f"│{l[:16].ljust(16)}│")
            print("└──────────────────┘")
            print()

            send_text_frame(ser, current_lines)

            for i in range(4):
                new_text = input(f"  Line {i+1}: ").strip()
                if new_text.lower() == 'q':
                    print("👋 退出")
                    return
                if new_text.lower() == 'c':
                    current_lines = ["", "", "", ""]
                    break
                if new_text:
                    current_lines[i] = new_text
                if i == 3:
                    break
            print("\033[F" * 10, end="")

    except KeyboardInterrupt:
        print("\n👋 退出交互模式")


# ============================================================
# 主入口
# ============================================================
def main():
    parser = argparse.ArgumentParser(
        description="STM32 + 12864B LCD 串口信息显示器",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  %(prog)s                           系统监控模式 (自动检测串口)
  %(prog)s -p COM3                   指定串口
  %(prog)s -t "CPU: 50%" "MEM: 2GB"  发送自定义 4 行文字
  %(prog)s -t "Line1" "Line2" "Line3" "Line4"
  %(prog)s -i logo.png               发送图片 (128x64 单色)
  %(prog)s -i photo.jpg --invert     反色显示图片
  %(prog)s --interactive             交互模式 (终端打字, LCD 实时显示)
  %(prog)s -b 115200                 指定波特率
        """
    )

    parser.add_argument('-p', '--port', default=None,
                        help='串口设备 (如 COM3, /dev/ttyUSB0), 不指定则自动查找')
    parser.add_argument('-b', '--baudrate', type=int, default=921600,
                        help='波特率 (默认 921600, STM32 固件使用此值)')
    parser.add_argument('-t', '--text', nargs='+', default=None,
                        help='自定义文字 (最多 4 行, 每行 16 字符)')
    parser.add_argument('-i', '--image', default=None,
                        help='图片文件路径 (转为 128x64 单色位图)')
    parser.add_argument('--invert', action='store_true',
                        help='图片反色显示')
    parser.add_argument('--interactive', action='store_true',
                        help='交互模式: 终端打字, LCD 实时显示')
    parser.add_argument('--interval', type=float, default=0.5,
                        help='系统监控刷新间隔 (秒, 默认 0.5)')
    parser.add_argument('--list', action='store_true',
                        help='列出可用串口后退出')

    args = parser.parse_args()

    # 列出串口
    if args.list:
        ports = list(serial.tools.list_ports.comports())
        if ports:
            print("可用串口:")
            for p in ports:
                print(f"  {p.device} - {p.description}")
        else:
            print("未找到串口设备")
        return

    # 检查依赖
    if args.image and not HAS_PIL:
        print("❌ 图片模式需要 Pillow 库: pip install Pillow")
        sys.exit(1)

    # 打开串口
    ser = open_serial(args.port, args.baudrate)

    try:
        if args.interactive:
            run_interactive(ser)
        elif args.image:
            run_image_mode(ser, args.image, args.invert)
        elif args.text:
            run_custom_text(ser, args.text)
        else:
            # 默认: 系统监控模式
            if not HAS_PSUTIL:
                print("⚠️  未安装 psutil, 使用时间显示模式")
                print("   安装: pip install psutil")
            run_system_monitor(ser, args.interval)
    finally:
        ser.close()
        print("🔌 串口已关闭")


if __name__ == '__main__':
    main()
