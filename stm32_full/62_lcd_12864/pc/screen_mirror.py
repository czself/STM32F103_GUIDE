#!/usr/bin/env python3
"""
STM32 + 12864B LCD 电脑画面实时镜像
=====================================

抓取电脑屏幕 → 缩放为 128x64 单色位图 → 串口发送 → STM32 → LCD 显示

用法:
  python screen_mirror.py                     # 屏幕镜像模式 (默认)
  python screen_mirror.py -p /dev/ttyUSB0     # 指定串口
  python screen_mirror.py --monitor 2         # 镜像第二个显示器
  python screen_mirror.py --crop 100 100 400 300  # 只抓取屏幕区域
  python screen_mirror.py -t "Line1" "Line2"  # 自定义文字模式
  python screen_mirror.py -i photo.png        # 图片模式
  python screen_mirror.py --monitor-info      # 系统信息模式
  python screen_mirror.py -f 15               # 目标帧率 (默认 10)

依赖:
  pip install pyserial Pillow
"""

import argparse
import os
import subprocess
import sys
import tempfile
import time

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("❌ 缺少 pyserial 库: pip install pyserial")
    sys.exit(1)

try:
    from PIL import Image, ImageGrab
    HAS_PIL = True
except ImportError:
    print("❌ 缺少 Pillow 库: pip install Pillow")
    sys.exit(1)

# 检查当前 Python 能否导入 dbus (Wayland 截图所需)
_HAS_DBUS = False
try:
    import dbus  # noqa: F811
    _HAS_DBUS = True
except ImportError:
    pass

# 可选的 mss 高性能截图 (Linux 推荐)
try:
    import mss
    HAS_MSS = True
except ImportError:
    HAS_MSS = False


# ============================================================
# 协议常量 (与 STM32 固件保持一致)
# ============================================================
FRAME_MODE_TEXT   = 0x01
FRAME_MODE_BITMAP = 0x02
FRAME_ACK = 0x55
LCD_W = 128
LCD_H = 64
BITMAP_SIZE = LCD_W * LCD_H // 8  # 1024 bytes


# ============================================================
# 串口
# ============================================================
def find_serial_port():
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        return None
    print("📡 可用串口:")
    for i, p in enumerate(ports):
        print(f"  [{i}] {p.device} - {p.description}")
    if len(ports) == 1:
        print(f"✅ 自动选择: {ports[0].device}")
        return ports[0].device
    for p in ports:
        kw = (p.description + str(p.manufacturer)).lower()
        if any(k in kw for k in ['ch340', 'cp210', 'ftdi', 'usb-serial', 'stlink']):
            print(f"✅ 自动选择: {p.device}")
            return p.device
    while True:
        try:
            return ports[int(input("选择编号: ").strip())].device
        except (ValueError, IndexError):
            print("无效编号")


def open_serial(port, baudrate=921600):
    if port is None:
        port = find_serial_port()
        if port is None:
            print("❌ 未找到串口, 指定: -p /dev/ttyUSB0")
            sys.exit(1)
    print(f"🔌 {port} @ {baudrate} baud")
    ser = serial.Serial(port, baudrate, timeout=1, write_timeout=1)
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    return ser


def wait_frame_ack(ser):
    ack = ser.read(1)
    if ack != bytes([FRAME_ACK]):
        raise serial.SerialTimeoutException(
            f"等待 STM32 ACK 超时或收到错误字节: {ack!r}"
        )


# ============================================================
# 屏幕截图 → 128x64 单色位图
# ============================================================
def _capture_gnome_wayland():
    """GNOME Wayland 截图 (通过 DBus, 无需安装任何工具)"""
    tmp = os.path.join(tempfile.gettempdir(), f'lcd_mirror_{os.getpid()}.png')
    try:
        os.unlink(tmp)
    except FileNotFoundError:
        pass

    result = subprocess.run([
        'gdbus', 'call', '--session',
        '--dest', 'org.gnome.Shell.Screenshot',
        '--object-path', '/org/gnome/Shell/Screenshot',
        '--method', 'org.gnome.Shell.Screenshot.Screenshot',
        'false', 'false',
        tmp
    ], capture_output=True, timeout=10)

    if result.returncode != 0 or not os.path.exists(tmp):
        raise OSError(f"GNOME Shell Screenshot 失败: {result.stderr.decode()}")

    img = Image.open(tmp).convert('RGB')
    try:
        os.unlink(tmp)
    except OSError:
        pass
    return img


def _capture_portal_dbus():
    """
    XDG Desktop Portal 截图 (使用 dbus-python, 正确处理异步响应)

    与 _capture_portal 文件轮询方式不同, 这个实现:
      1. 直接用 dbus 调用 Screenshot
      2. 监听 Response 信号获取 URI 或文件描述符
      3. 从返回的 URI 读取截图文件

    这是 GNOME Wayland 下唯一可靠的截图方式。
    """
    import uuid
    try:
        import dbus
        import dbus.mainloop.glib
        from gi.repository import GLib
    except ImportError as e:
        raise ImportError(
            f"缺少系统 Python 模块: {e}\n"
            "PlatformIO venv 看不到系统包, 请用系统 Python 运行:\n"
            "  /usr/bin/python3 screen_mirror.py -p /dev/ttyUSB0"
        )

    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SessionBus()

    # 获取 Screenshot 接口
    portal = bus.get_object('org.freedesktop.portal.Desktop',
                            '/org/freedesktop/portal/desktop')
    iface = dbus.Interface(portal, 'org.freedesktop.portal.Screenshot')

    token = f"lcd_{uuid.uuid4().hex[:8]}"
    result = {'uri': None, 'error': None}
    loop = GLib.MainLoop()

    def on_response(response_code, results):
        """Portal Response 信号回调"""
        if response_code == 0:  # 0 = success
            # 优先尝试 URI
            if 'uri' in results:
                result['uri'] = str(results['uri'])
            else:
                result['error'] = 'Response 中缺少 uri'
        else:
            result['error'] = f'Portal 返回错误码 {response_code}'
        loop.quit()

    def on_timeout():
        result['error'] = '截图超时 (10s)'
        loop.quit()
        return False

    try:
        # 调用 Screenshot
        request_path = iface.Screenshot(
            '',  # parent_window
            {
                'handle_token': token,
                'interactive': False,
            }
        )

        # 监听该 request 的 Response 信号
        bus.add_signal_receiver(
            on_response,
            signal_name='Response',
            dbus_interface='org.freedesktop.portal.Request',
            path=request_path
        )

        # 10 秒超时
        GLib.timeout_add_seconds(10, on_timeout)

        # 阻塞等待
        loop.run()

        if result['error']:
            raise OSError(result['error'])

        # 处理 URI
        uri = result['uri']
        if not uri:
            raise OSError('Portal 未返回 URI')

        # uri 格式: file:///path/to/file.png
        if uri.startswith('file://'):
            path = uri[7:]
        else:
            path = uri

        if not os.path.exists(path) or os.path.getsize(path) == 0:
            raise OSError(f'截图文件不存在或为空: {path}')

        img = Image.open(path).convert('RGB')
        try:
            os.unlink(path)
        except OSError:
            pass
        return img

    except dbus.exceptions.DBusException as e:
        raise OSError(f'DBus 错误: {e}')


def _capture_gnome_screenshot():
    """gnome-screenshot CLI (需要 apt install gnome-screenshot)"""
    tmp = os.path.join(tempfile.gettempdir(), f'lcd_mirror_{os.getpid()}.png')
    subprocess.run(['gnome-screenshot', '-f', tmp],
                   capture_output=True, timeout=10, check=True)
    img = Image.open(tmp).convert('RGB')
    os.unlink(tmp)
    return img


def _capture_x11():
    """X11 截图 (PIL ImageGrab)"""
    img = ImageGrab.grab(all_screens=True)
    return img.convert('RGB')


def _capture_mss(monitor=0, crop=None):
    """mss 高性能截图"""
    with mss.MSS() as sct:
        monitors = sct.monitors
        idx = monitor + 1 if monitor > 0 else 1
        if idx >= len(monitors):
            idx = 1
        mon = monitors[idx]

        if crop:
            region = {
                'left': mon['left'] + crop[0], 'top': mon['top'] + crop[1],
                'width': crop[2], 'height': crop[3],
            }
        else:
            region = {
                'left': mon['left'], 'top': mon['top'],
                'width': mon['width'], 'height': mon['height'],
            }

        sct_img = sct.grab(region)
        return Image.frombytes('RGB', (sct_img.width, sct_img.height),
                               sct_img.rgb, 'raw', 'BGRX')


def capture_screen(monitor=0, crop=None):
    """
    截取屏幕, 返回 PIL Image (RGB)

    自动检测并选择最佳截图方式:
      1. XDG Desktop Portal (Wayland GNOME/KDE 通用, 无需安装)
      2. GNOME Shell Screenshot DBus
      3. mss 高性能截图 (X11/Windows/macOS)
      4. PIL ImageGrab (X11)
    """
    session_type = os.environ.get('XDG_SESSION_TYPE', '').lower()
    desktop = os.environ.get('XDG_CURRENT_DESKTOP', '').lower()

    # Wayland: 优先用 XDG Desktop Portal (通过 dbus-python)
    if session_type == 'wayland':
        try:
            return _capture_portal_dbus()
        except Exception as e:
            print(f"  ⚠️  Portal 截图失败: {e}")
            print("   💡 试试用系统 Python 运行: /usr/bin/python3 screen_mirror.py -p /dev/ttyUSB0")

    # X11 / Windows / macOS: 优先 mss
    if HAS_MSS:
        try:
            return _capture_mss(monitor, crop)
        except Exception:
            pass

    # X11 PIL
    try:
        return _capture_x11()
    except Exception:
        pass

    raise OSError(
        "无法截图!\n\n"
        "  如果看到 '截图权限' 弹窗, 请点击 '允许'\n"
        "  或者切换登录到 'Ubuntu on Xorg' (X11 模式)"
    )


def floyd_steinberg_dither(gray_img):
    """
    Floyd-Steinberg 误差扩散抖动
    将灰度图 (0-255) 转为 1-bit 单色, 效果远好于简单阈值
    """
    img = gray_img.copy()
    pixels = img.load()

    for y in range(LCD_H):
        for x in range(LCD_W):
            old = pixels[x, y]
            new = 0 if old < 128 else 255
            pixels[x, y] = new
            err = old - new

            if x + 1 < LCD_W:
                pixels[x + 1, y] = min(255, max(0, pixels[x + 1, y] + err * 7 // 16))
            if y + 1 < LCD_H:
                if x > 0:
                    pixels[x - 1, y + 1] = min(255, max(0, pixels[x - 1, y + 1] + err * 3 // 16))
                pixels[x, y + 1] = min(255, max(0, pixels[x, y + 1] + err * 5 // 16))
                if x + 1 < LCD_W:
                    pixels[x + 1, y + 1] = min(255, max(0, pixels[x + 1, y + 1] + err * 1 // 16))
    return img


def screen_to_bitmap(monitor=0, crop=None, invert=False, dither=True):
    """
    截屏 → 缩放 → 抖动 → 1024 字节位图

    返回 bytes (1024 bytes)
    """
    img = capture_screen(monitor, crop)

    # 转为灰度并缩放到 128x64
    gray = img.convert('L').resize((LCD_W, LCD_H), Image.LANCZOS)

    if dither:
        gray = floyd_steinberg_dither(gray)

    # 打包为位图: 每字节 8 个水平像素, MSB=最左
    pixels = gray.load()
    bitmap = bytearray(BITMAP_SIZE)

    for y in range(LCD_H):
        for x_byte in range(LCD_W // 8):  # 0..15
            byte_val = 0
            for bit in range(8):
                px = pixels[x_byte * 8 + bit, y]
                bit_val = 1 if (px < 128) != invert else 0
                byte_val |= (bit_val << (7 - bit))
            bitmap[y * (LCD_W // 8) + x_byte] = byte_val

    return bytes(bitmap)


def send_bitmap_frame(ser, bitmap):
    """发送位图帧: [0xAA 0x02 1024_bytes]"""
    frame = bytearray([0xAA, FRAME_MODE_BITMAP])
    frame.extend(bitmap)
    CHUNK = 512
    for i in range(0, len(frame), CHUNK):
        ser.write(frame[i:i + CHUNK])
    ser.flush()
    wait_frame_ack(ser)


def send_text_frame(ser, lines):
    """发送文本帧: [0xAA 0x01 64_bytes]"""
    frame = bytearray([0xAA, FRAME_MODE_TEXT])
    for i in range(4):
        txt = str(lines[i]) if i < len(lines) else ""
        txt = txt[:16].ljust(16)
        frame.extend(txt.encode('ascii', errors='replace'))
    ser.write(bytes(frame))
    ser.flush()
    wait_frame_ack(ser)


# ============================================================
# 屏幕镜像模式 (默认)
# ============================================================
def run_screen_mirror(ser, monitor=0, crop=None, fps=10, invert=False, dither=True):
    """屏幕镜像: 持续截图 → LCD"""
    frame_time = 1.0 / fps
    print(f"\n🖥️  屏幕镜像模式")
    print(f"   LCD: 128x64 单色 | 目标: {fps} fps | 串口: {ser.baudrate} baud")
    if dither:
        print(f"   抖动: Floyd-Steinberg")
    print(f"   按 Ctrl+C 退出\n")

    frame_count = 0
    start_time = time.time()
    last_report = start_time

    try:
        while True:
            t0 = time.time()

            # 截图 + 转位图
            bitmap = screen_to_bitmap(monitor, crop, invert, dither)

            # 发送
            send_bitmap_frame(ser, bitmap)

            frame_count += 1
            elapsed = time.time() - t0

            # 帧率控制
            sleep_time = frame_time - elapsed
            if sleep_time > 0:
                time.sleep(sleep_time)

            # 每秒统计
            now = time.time()
            if now - last_report >= 1.0:
                actual_fps = frame_count / (now - start_time)
                bw = frame_count * 1026 / (now - start_time)
                print(f"\r  📊 {actual_fps:.1f} fps | {bw/1000:.1f} KB/s | "
                      f"延迟 {elapsed*1000:.0f}ms   ", end='', flush=True)
                frame_count = 0
                start_time = now
                last_report = now

    except KeyboardInterrupt:
        print("\n\n👋 退出屏幕镜像")


# ============================================================
# 其他模式
# ============================================================
def run_system_monitor(ser, interval=0.5):
    """系统监控模式"""
    try:
        import psutil
    except ImportError:
        print("❌ 需要 psutil: pip install psutil")
        return

    print(f"\n📊 系统监控 (刷新: {interval}s) | Ctrl+C 退出\n")
    try:
        while True:
            cpu = psutil.cpu_percent(interval=0.1)
            mem = psutil.virtual_memory()
            disk = psutil.disk_usage('/')

            lines = [
                f"CPU {cpu:>5.1f}% {'#' * int(cpu/10)}{'-' * (10-int(cpu/10))}",
                f"MEM {mem.percent:>5.1f}% {'#' * int(mem.percent/10)}{'-' * (10-int(mem.percent/10))}",
                f"DSK {disk.percent:>5.1f}% {'#' * int(disk.percent/10)}{'-' * (10-int(disk.percent/10))}",
                time.strftime("TIME %H:%M:%S  "),
            ]

            print("\033[F" * 5, end="")
            print("┌──────────────────┐")
            for l in lines:
                print(f"│{l[:16].ljust(16)}│")
            print("└──────────────────┘")

            send_text_frame(ser, lines)
            time.sleep(interval)
    except KeyboardInterrupt:
        print("\n👋 退出")


def run_image_mode(ser, path, invert=False):
    """将图片文件发送到 LCD"""
    print(f"🖼️  {path}")
    img = Image.open(path).convert('L').resize((LCD_W, LCD_H), Image.LANCZOS)
    img = floyd_steinberg_dither(img)

    pixels = img.load()
    bitmap = bytearray(BITMAP_SIZE)
    for y in range(LCD_H):
        for xb in range(LCD_W // 8):
            bv = 0
            for bit in range(8):
                p = pixels[xb * 8 + bit, y]
                v = 1 if (p < 128) != invert else 0
                bv |= (v << (7 - bit))
            bitmap[y * (LCD_W // 8) + xb] = bv

    send_bitmap_frame(ser, bytes(bitmap))
    print("✅ 发送完成")


# ============================================================
# 入口
# ============================================================
def main():
    parser = argparse.ArgumentParser(
        description="电脑画面 → STM32 → 12864B LCD 实时镜像",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  %(prog)s                       屏幕镜像 (默认, 自动检测串口)
  %(prog)s -p /dev/ttyUSB0      指定串口
  %(prog)s -f 15                 目标 15 fps (默认 10)
  %(prog)s --monitor 2           镜像第二个显示器
  %(prog)s --crop 0 0 400 300   只抓取左上角 400x300 区域
  %(prog)s --invert              反色显示
  %(prog)s --no-dither           关闭抖动 (纯阈值, 更快但画质差)
  %(prog)s --monitor-info        系统信息模式 (CPU/内存)
  %(prog)s -t "Line1" "Line2" "Line3" "Line4"
  %(prog)s -i photo.png
        """
    )
    parser.add_argument('-p', '--port', default=None, help='串口设备')
    parser.add_argument('-b', '--baudrate', type=int, default=921600, help='波特率 (默认 921600)')
    parser.add_argument('-f', '--fps', type=int, default=10, help='目标帧率 (默认 10)')
    parser.add_argument('--monitor', type=int, default=0, help='显示器编号 (0=主屏)')
    parser.add_argument('--crop', nargs=4, type=int, metavar=('X', 'Y', 'W', 'H'), help='截取区域')
    parser.add_argument('--invert', action='store_true', help='反色')
    parser.add_argument('--no-dither', action='store_true', help='关闭抖动 (更快)')
    parser.add_argument('--monitor-info', action='store_true', help='系统监控模式')
    parser.add_argument('-t', '--text', nargs='+', help='自定义文字 (4行)')
    parser.add_argument('-i', '--image', default=None, help='图片文件')
    parser.add_argument('--list', action='store_true', help='列出串口')

    args = parser.parse_args()

    if args.list:
        ports = list(serial.tools.list_ports.comports())
        for p in ports:
            print(f"  {p.device} - {p.description}")
        return

    ser = open_serial(args.port, args.baudrate)

    try:
        if args.image:
            run_image_mode(ser, args.image, args.invert)
        elif args.text:
            print("📝 自定义文字"); send_text_frame(ser, args.text)
            print("✅ 已发送")
        elif args.monitor_info:
            run_system_monitor(ser)
        else:
            # 默认: 屏幕镜像
            crop = tuple(args.crop) if args.crop else None
            run_screen_mirror(ser, args.monitor, crop, args.fps,
                             args.invert, not args.no_dither)
    finally:
        ser.close()
        print("🔌 串口已关闭")


if __name__ == '__main__':
    main()
