#!/usr/bin/env python3
import argparse
from datetime import datetime
from pathlib import Path
import queue
import socket
import struct
import threading
import time

import cv2
import numpy as np
from PySide6 import QtCore, QtGui, QtWidgets


HEADER = struct.Struct("<4sIIHH")
CONTROL_SCALE = struct.Struct("<4sHH")
CONTROL_EV = struct.Struct("<4shH")
CONTROL_I16 = struct.Struct("<4shH")
CONTROL_U16 = struct.Struct("<4sHH")
CONTROL_U32 = struct.Struct("<4sI")
CONTROL_FOCUS = b"AF00"
DEVICE_STALE_SECONDS = 10.0
RESOLUTIONS = {
    "auto": None,
    "640x480": (640, 480),
    "640x360": (640, 360),
    "480x270": (480, 270),
    "320x240": (320, 240),
    "320x180": (320, 180),
    "176x144": (176, 144),
}
RESOLUTION_CHOICES = [
    ("Keep", None),
    ("640 x 480", (640, 480)),
    ("640 x 360", (640, 360)),
    ("480 x 270", (480, 270)),
    ("320 x 240", (320, 240)),
    ("320 x 180", (320, 180)),
    ("176 x 144", (176, 144)),
]
ROTATION_CHOICES = [
    ("0 deg", 0),
    ("90 deg", 90),
    ("180 deg", 180),
    ("270 deg", 270),
]
SENSOR_EV_CHOICES = [
    ("-2 EV", -2),
    ("-1 EV", -1),
    ("0 EV", 0),
    ("+1 EV", 1),
    ("+2 EV", 2),
    ("+3 EV", 3),
]
BANDING_CHOICES = [
    ("Auto", 0),
    ("50 Hz", 50),
    ("60 Hz", 60),
]


def parse_resolution(value):
    if value in RESOLUTIONS:
        return RESOLUTIONS[value]
    try:
        width, height = value.lower().split("x", 1)
        width = int(width)
        height = int(height)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("resolution must be auto or WIDTHxHEIGHT") from exc
    if width <= 0 or height <= 0 or (width % 16) or (height & 1):
        raise argparse.ArgumentTypeError("resolution width must align to 16 pixels and height must be even")
    return width, height


def resolution_index(size):
    if size is None:
        return 0
    for index, (_name, value) in enumerate(RESOLUTION_CHOICES):
        if value == size:
            return index
    return 0


def rotation_index(rotate):
    for index, (_name, value) in enumerate(ROTATION_CHOICES):
        if value == rotate:
            return index
    return 0


def recv_exact(conn, size):
    chunks = []
    remaining = size
    while remaining:
        try:
            data = conn.recv(remaining)
        except socket.timeout:
            if chunks:
                continue
            raise
        if not data:
            raise ConnectionError("socket closed")
        chunks.append(data)
        remaining -= len(data)
    return b"".join(chunks)


def rgb565_to_bgr(frame_bytes, width, height, byte_order):
    raw = np.frombuffer(frame_bytes, dtype=np.uint8).reshape((height, width, 2))
    if byte_order == "swap16":
        pixel = raw[..., 1].astype(np.uint16) | (raw[..., 0].astype(np.uint16) << 8)
    else:
        pixel = raw[..., 0].astype(np.uint16) | (raw[..., 1].astype(np.uint16) << 8)

    r = ((pixel >> 11) & 0x1F).astype(np.uint16) * 255 // 31
    g = ((pixel >> 5) & 0x3F).astype(np.uint16) * 255 // 63
    b = (pixel & 0x1F).astype(np.uint16) * 255 // 31
    return np.dstack((b, g, r)).astype(np.uint8)


def yuv422_to_bgr(frame_bytes, width, height, order):
    raw = np.frombuffer(frame_bytes, dtype=np.uint8).reshape((height, width // 2, 4))

    if order == "yuyv":
        y0, u, y1, v = raw[..., 0], raw[..., 1], raw[..., 2], raw[..., 3]
    elif order == "uyvy":
        u, y0, v, y1 = raw[..., 0], raw[..., 1], raw[..., 2], raw[..., 3]
    elif order == "yvyu":
        y0, v, y1, u = raw[..., 0], raw[..., 1], raw[..., 2], raw[..., 3]
    elif order == "vyuy":
        v, y0, u, y1 = raw[..., 0], raw[..., 1], raw[..., 2], raw[..., 3]
    else:
        raise ValueError(f"unsupported yuv422 order: {order}")

    y = np.empty((height, width), dtype=np.float32)
    y[:, 0::2] = y0.astype(np.float32)
    y[:, 1::2] = y1.astype(np.float32)
    u = np.repeat(u.astype(np.float32) - 128.0, 2, axis=1)
    v = np.repeat(v.astype(np.float32) - 128.0, 2, axis=1)

    r = y + 1.402 * v
    g = y - 0.344136 * u - 0.714136 * v
    b = y + 1.772 * u
    return np.dstack((b, g, r)).clip(0, 255).astype(np.uint8)


def rotate_image(image, rotate):
    if rotate == 90:
        return cv2.rotate(image, cv2.ROTATE_90_CLOCKWISE)
    if rotate == 180:
        return cv2.rotate(image, cv2.ROTATE_180)
    if rotate == 270:
        return cv2.rotate(image, cv2.ROTATE_90_COUNTERCLOCKWISE)
    return image


def flip_image(image, flip):
    if flip == "x":
        return cv2.flip(image, 0)
    if flip == "y":
        return cv2.flip(image, 1)
    if flip == "both":
        return cv2.flip(image, -1)
    return image


def make_gamma_lut(gamma_percent):
    gamma = max(gamma_percent / 100.0, 0.1)
    inv_gamma = 1.0 / gamma
    table = np.array([(i / 255.0) ** inv_gamma * 255.0 for i in range(256)])
    return table.clip(0, 255).astype(np.uint8)


def apply_picture_params(image, params):
    b_gain = params["b_gain"]
    g_gain = params["g_gain"]
    r_gain = params["r_gain"]
    brightness = params["brightness"]
    contrast = params["contrast"]
    saturation = params["saturation"]
    gamma = params["gamma"]
    sharpness = params["sharpness"]

    if b_gain != 100 or g_gain != 100 or r_gain != 100:
        gains = np.array([b_gain, g_gain, r_gain], dtype=np.float32) / 100.0
        image = np.clip(image.astype(np.float32) * gains, 0, 255).astype(np.uint8)

    if contrast != 100 or brightness != 0:
        image = cv2.convertScaleAbs(image, alpha=contrast / 100.0, beta=brightness)

    if saturation != 100:
        hsv = cv2.cvtColor(image, cv2.COLOR_BGR2HSV).astype(np.float32)
        hsv[..., 1] = np.clip(hsv[..., 1] * (saturation / 100.0), 0, 255)
        image = cv2.cvtColor(hsv.astype(np.uint8), cv2.COLOR_HSV2BGR)

    if gamma != 100:
        image = cv2.LUT(image, make_gamma_lut(gamma))

    if sharpness > 0:
        blurred = cv2.GaussianBlur(image, (0, 0), 1.0)
        amount = sharpness / 100.0
        image = cv2.addWeighted(image, 1.0 + amount, blurred, -amount, 0)

    return image


def bgr_to_qimage(image):
    rgb = np.ascontiguousarray(cv2.cvtColor(image, cv2.COLOR_BGR2RGB))
    height, width, channels = rgb.shape
    bytes_per_line = rgb.strides[0]
    return QtGui.QImage(rgb.data, width, height, bytes_per_line, QtGui.QImage.Format_RGB888).copy()


class CameraReceiver(QtCore.QThread):
    frame_ready = QtCore.Signal(QtGui.QImage, object, int, int, int, int)
    status_changed = QtCore.Signal(str)
    metrics_changed = QtCore.Signal(str)
    connected_changed = QtCore.Signal(bool, str)

    def __init__(self, args):
        super().__init__()
        self.args = args
        self._stop = threading.Event()
        self._control_queue = queue.Queue()
        self._socket_lock = threading.Lock()
        self._server = None
        self._conn = None
        self._pending_resolution = None
        self._drop_frames = 0
        self._epoch_lock = threading.Lock()
        self._next_epoch = 0
        self._active_epoch = 0
        self._pending_epoch = None
        self._last_seq_seen = 0
        self._switch_min_seq = 0
        self._desired_resolution = args.resolution
        self._desired_ev = 0
        self._desired_ae_ag = bool(args.sensor_auto_ae)
        self._desired_shutter = int(args.sensor_shutter)
        self._desired_gain16 = int(args.sensor_gain16)
        self._desired_ae_target = int(args.sensor_ae_target)
        self._desired_banding = int(args.sensor_banding)
        self._desired_rotate = int(args.rotate)
        self._transform_lock = threading.Lock()
        self._rotate = args.rotate
        self._params_lock = threading.Lock()
        self._params = {
            "b_gain": args.b_gain,
            "g_gain": args.g_gain,
            "r_gain": args.r_gain,
            "brightness": args.brightness,
            "contrast": args.contrast,
            "saturation": args.saturation,
            "gamma": args.gamma,
            "sharpness": args.sharpness,
        }

    def stop(self):
        self._stop.set()
        with self._socket_lock:
            for sock in (self._conn, self._server):
                if sock is not None:
                    try:
                        sock.shutdown(socket.SHUT_RDWR)
                    except OSError:
                        pass
                    try:
                        sock.close()
                    except OSError:
                        pass

    def request_resolution(self, size):
        if size is not None:
            with self._epoch_lock:
                self._next_epoch += 1
                epoch = self._next_epoch
            self._desired_resolution = size
            self._control_queue.put(("scale", size, epoch))
            return epoch
        with self._epoch_lock:
            return self._next_epoch

    def request_focus(self):
        self._control_queue.put(("focus", None, None))

    def request_sensor_ev(self, ev):
        self._desired_ev = int(ev)
        self._control_queue.put(("ev", int(ev), None))

    def request_sensor_ae_ag(self, enable):
        self._desired_ae_ag = bool(enable)
        self._control_queue.put(("ae_ag", 1 if enable else 0, None))

    def request_sensor_shutter(self, shutter):
        self._desired_shutter = int(shutter)
        self._control_queue.put(("shutter", int(shutter), None))

    def request_sensor_gain16(self, gain16):
        self._desired_gain16 = int(gain16)
        self._control_queue.put(("gain16", int(gain16), None))

    def request_sensor_ae_target(self, target):
        self._desired_ae_target = int(target)
        self._control_queue.put(("ae_target", int(target), None))

    def request_sensor_banding(self, light_freq):
        self._desired_banding = int(light_freq)
        self._control_queue.put(("banding", int(light_freq), None))

    def set_rotate(self, rotate):
        rotate = int(rotate)
        with self._transform_lock:
            self._rotate = rotate
        self._desired_rotate = rotate
        self._control_queue.put(("rotate", rotate, None))

    def rotate(self):
        with self._transform_lock:
            return self._rotate

    def set_picture_params(self, **params):
        with self._params_lock:
            self._params.update(params)

    def picture_params(self):
        with self._params_lock:
            return dict(self._params)

    def reset_connection_state(self):
        while True:
            try:
                self._control_queue.get_nowait()
            except queue.Empty:
                break

        self._pending_resolution = None
        self._drop_frames = 0
        self._pending_epoch = None
        self._last_seq_seen = 0
        self._switch_min_seq = 0

        if self._desired_resolution is not None:
            self._control_queue.put(("scale", self._desired_resolution, self._next_epoch))
        self._control_queue.put(("ev", self._desired_ev, None))
        self._control_queue.put(("ae_ag", 1 if self._desired_ae_ag else 0, None))
        self._control_queue.put(("ae_target", self._desired_ae_target, None))
        self._control_queue.put(("banding", self._desired_banding, None))
        self._control_queue.put(("rotate", self._desired_rotate, None))
        if not self._desired_ae_ag:
            self._control_queue.put(("shutter", self._desired_shutter, None))
            self._control_queue.put(("gain16", self._desired_gain16, None))

    def send_pending_controls(self, conn):
        last_size = None
        last_epoch = None
        last_ev = None
        last_ae_ag = None
        last_shutter = None
        last_gain16 = None
        last_ae_target = None
        last_banding = None
        last_rotate = None
        focus_requested = False
        while True:
            try:
                command, value, epoch = self._control_queue.get_nowait()
            except queue.Empty:
                break
            if command == "scale":
                last_size = value
                last_epoch = epoch
            elif command == "focus":
                focus_requested = True
            elif command == "ev":
                last_ev = value
            elif command == "ae_ag":
                last_ae_ag = value
            elif command == "shutter":
                last_shutter = value
            elif command == "gain16":
                last_gain16 = value
            elif command == "ae_target":
                last_ae_target = value
            elif command == "banding":
                last_banding = value
            elif command == "rotate":
                last_rotate = value
        if focus_requested:
            conn.sendall(CONTROL_FOCUS)
            self.status_changed.emit("focus requested")
        if last_ev is not None:
            conn.sendall(CONTROL_EV.pack(b"AEV0", last_ev, 0))
            self.status_changed.emit(f"sensor ev {last_ev:+d}")
        if last_ae_ag is not None:
            conn.sendall(CONTROL_I16.pack(b"AEG0", last_ae_ag, 0))
            self.status_changed.emit("sensor ae/ag auto" if last_ae_ag else "sensor ae/ag manual")
        if last_ae_target is not None:
            conn.sendall(CONTROL_U16.pack(b"AET0", last_ae_target, 0))
            self.status_changed.emit(f"sensor ae target {last_ae_target}")
        if last_banding is not None:
            conn.sendall(CONTROL_I16.pack(b"BND0", last_banding, 0))
            label = "auto" if last_banding == 0 else f"{last_banding} Hz"
            self.status_changed.emit(f"sensor banding {label}")
        if last_shutter is not None:
            conn.sendall(CONTROL_U32.pack(b"SHT0", last_shutter))
            self.status_changed.emit(f"sensor shutter {last_shutter}")
        if last_gain16 is not None:
            conn.sendall(CONTROL_U16.pack(b"AGN0", last_gain16, 0))
            self.status_changed.emit(f"sensor gain16 {last_gain16}")
        if last_rotate is not None:
            conn.sendall(CONTROL_U16.pack(b"ROT0", last_rotate, 0))
            self.status_changed.emit(f"device rotate {last_rotate} deg")
        if last_size is not None:
            conn.sendall(CONTROL_SCALE.pack(b"SCL0", last_size[0], last_size[1]))
            self._pending_resolution = last_size
            self._pending_epoch = last_epoch
            self._switch_min_seq = self._last_seq_seen
            self._drop_frames = 0
            self.status_changed.emit(f"sent scale {last_size[0]}x{last_size[1]}")

    def run(self):
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.settimeout(0.5)
        with self._socket_lock:
            self._server = server

        try:
            server.bind((self.args.host, self.args.port))
            server.listen(1)
            self.status_changed.emit(f"listening on {self.args.host}:{self.args.port}")
        except OSError as exc:
            self.status_changed.emit(f"listen failed: {exc}")
            server.close()
            return

        while not self._stop.is_set():
            try:
                conn, addr = server.accept()
            except socket.timeout:
                continue
            except OSError as exc:
                self.status_changed.emit(f"accept failed: {exc}")
                break

            self.status_changed.emit(f"connected: {addr[0]}:{addr[1]}")
            self.connected_changed.emit(True, addr[0])
            conn.settimeout(1.0)
            with self._socket_lock:
                self._conn = conn
            self.reset_connection_state()
            try:
                self.receive_loop(conn)
            except Exception as exc:
                self.status_changed.emit(f"disconnected: {exc}")
                self.connected_changed.emit(False, str(exc))
            finally:
                with self._socket_lock:
                    if self._conn is conn:
                        self._conn = None
                try:
                    conn.close()
                except OSError:
                    pass

        with self._socket_lock:
            self._server = None
        try:
            server.close()
        except OSError:
            pass
        self.status_changed.emit("stopped")

    def receive_loop(self, conn):
        frames = 0
        bytes_total = 0
        last = time.time()

        while not self._stop.is_set():
            self.send_pending_controls(conn)

            try:
                header = recv_exact(conn, HEADER.size)
            except socket.timeout:
                continue
            magic, seq, length, width, height = HEADER.unpack(header)
            if magic not in (b"RGB5", b"YUV2"):
                raise ValueError(f"unsupported stream magic: {magic!r}")

            expected_len = width * height * 2
            if length != expected_len:
                raise ValueError(f"bad frame length: {length}, expected {expected_len}")

            payload = recv_exact(conn, length)
            self._last_seq_seen = seq
            if self._pending_resolution is not None:
                if seq <= self._switch_min_seq:
                    continue
                if (width, height) != self._pending_resolution:
                    continue
                if self._drop_frames > 0:
                    self._drop_frames -= 1
                    continue
                self._pending_resolution = None
                if self._pending_epoch is not None:
                    self._active_epoch = self._pending_epoch
                    self._pending_epoch = None

            if magic == b"YUV2" or self.args.mode == "yuv422":
                image = yuv422_to_bgr(payload, width, height, self.args.order)
            else:
                image = rgb565_to_bgr(payload, width, height, self.args.byte_order)
            image = rotate_image(image, self.rotate())
            image = flip_image(image, self.args.flip)
            image = apply_picture_params(image, self.picture_params())
            qimage = bgr_to_qimage(image)
            self.frame_ready.emit(qimage, image, width, height, seq, self._active_epoch)

            frames += 1
            bytes_total += length
            now = time.time()
            if now - last >= 1.0:
                elapsed = now - last
                fps = frames / elapsed
                mbps = bytes_total * 8.0 / elapsed / 1_000_000.0
                if magic == b"YUV2":
                    fmt = "YUV422"
                else:
                    fmt = "RGB565"
                self.metrics_changed.emit(f"{fps:.1f} fps  {mbps:.1f} Mbps  {width}x{height}  {fmt}")
                frames = 0
                bytes_total = 0
                last = now


class ConnectionDialog(QtWidgets.QDialog):
    def __init__(self, args, receiver, parent=None):
        super().__init__(parent)
        self.args = args
        self.receiver = receiver
        self.setWindowTitle("Camera Connection")
        self.setModal(True)
        self.resize(460, 140)

        self.status = QtWidgets.QLabel(
            f"Listening on {args.host}:{args.port}, waiting for camera client connection..."
        )
        self.connect_button = QtWidgets.QPushButton("Connect")
        self.cancel_button = QtWidgets.QPushButton("Cancel")

        buttons = QtWidgets.QHBoxLayout()
        buttons.addStretch(1)
        buttons.addWidget(self.connect_button)
        buttons.addWidget(self.cancel_button)

        layout = QtWidgets.QVBoxLayout(self)
        layout.addWidget(self.status)
        layout.addLayout(buttons)

        self.receiver.connected_changed.connect(self.on_receiver_connected)
        self.receiver.status_changed.connect(self.on_receiver_status)
        self.connect_button.clicked.connect(self.accept)
        self.cancel_button.clicked.connect(self.reject)

    def start(self):
        self.status.setText(
            f"Listening on {self.args.host}:{self.args.port}, waiting for camera client connection..."
        )

    def on_receiver_status(self, text):
        if text.startswith("listen failed"):
            self.status.setText(text)
        elif text.startswith("listening"):
            self.status.setText(f"{text}; waiting for camera client connection...")

    def on_receiver_connected(self, connected, text):
        if connected:
            self.status.setText(f"Connected {text}")
            self.accept()

    def accept(self):
        super().accept()

    def reject(self):
        super().reject()
class VideoLabel(QtWidgets.QLabel):
    sample_requested = QtCore.Signal(int, int)

    def __init__(self):
        super().__init__()
        self.setMinimumSize(640, 360)
        self.setAlignment(QtCore.Qt.AlignCenter)
        self.setMouseTracking(True)
        self.setStyleSheet("background:#101418; color:#9aa4ad;")
        self.setText("Waiting for camera stream")
        self._pixmap = None
        self._image_width = 0
        self._image_height = 0
        self._image_rect = QtCore.QRect()

    def clear_image(self, text):
        self._pixmap = None
        self._image_width = 0
        self._image_height = 0
        self._image_rect = QtCore.QRect()
        self.clear()
        self.setText(text)

    def set_image(self, image):
        self._pixmap = QtGui.QPixmap.fromImage(image)
        self._image_width = image.width()
        self._image_height = image.height()
        self.update_pixmap()

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self.update_pixmap()

    def update_pixmap(self):
        if self._pixmap is None:
            return
        scaled = self._pixmap.scaled(self.size(), QtCore.Qt.KeepAspectRatio, QtCore.Qt.SmoothTransformation)
        left = (self.width() - scaled.width()) // 2
        top = (self.height() - scaled.height()) // 2
        self._image_rect = QtCore.QRect(left, top, scaled.width(), scaled.height())
        self.setPixmap(scaled)

    def mouseMoveEvent(self, event):
        if self._pixmap is None or self._image_width <= 0 or self._image_height <= 0:
            return
        pos = event.position().toPoint()
        if not self._image_rect.contains(pos):
            return
        x = int((pos.x() - self._image_rect.left()) * self._image_width / self._image_rect.width())
        y = int((pos.y() - self._image_rect.top()) * self._image_height / self._image_rect.height())
        x = max(0, min(self._image_width - 1, x))
        y = max(0, min(self._image_height - 1, y))
        self.sample_requested.emit(x, y)


class MainWindow(QtWidgets.QMainWindow):
    def __init__(self, args):
        super().__init__()
        self.args = args
        self.receiver = CameraReceiver(args)
        device_ip = getattr(args, "device_ip", None)
        if device_ip:
            self.setWindowTitle(f"Titan Mini Camera - {device_ip} tcp:{args.port}")
        else:
            self.setWindowTitle(f"Titan Mini Camera - tcp:{args.port}")
        self.resize(1180, 760)

        self.video = VideoLabel()
        self.status = QtWidgets.QLabel("Idle")
        self.metrics = QtWidgets.QLabel("-")
        self.pixel = QtWidgets.QLabel("-")
        self.current_epoch = 0
        self.expected_resolution = args.resolution
        self.last_frame_bgr = None
        self.recording = False
        self.record_writer = None
        self.record_path = None
        self.record_size = None
        self._closing = False
        self._reconnect_dialog_open = False
        self._reconnect_dialog_enabled = False
        self._initial_resolution_sent = False
        self.resolution = QtWidgets.QComboBox()
        for name, _size in RESOLUTION_CHOICES:
            self.resolution.addItem(name)
        self.resolution.setCurrentIndex(resolution_index(args.resolution))

        self.rotation = QtWidgets.QComboBox()
        for name, rotate in ROTATION_CHOICES:
            self.rotation.addItem(name, rotate)
        self.rotation.setCurrentIndex(rotation_index(args.rotate))

        self.sensor_ev = QtWidgets.QComboBox()
        for name, ev in SENSOR_EV_CHOICES:
            self.sensor_ev.addItem(name, ev)
        self.sensor_ev.setCurrentIndex(2)

        self.sensor_auto_ae = QtWidgets.QCheckBox()
        self.sensor_auto_ae.setChecked(bool(args.sensor_auto_ae))

        self.sensor_ae_target = QtWidgets.QSpinBox()
        self.sensor_ae_target.setRange(1, 255)
        self.sensor_ae_target.setValue(args.sensor_ae_target)

        self.sensor_banding = QtWidgets.QComboBox()
        for name, value in BANDING_CHOICES:
            self.sensor_banding.addItem(name, value)
        self.sensor_banding.setCurrentIndex(0)
        for index in range(self.sensor_banding.count()):
            if self.sensor_banding.itemData(index) == args.sensor_banding:
                self.sensor_banding.setCurrentIndex(index)
                break

        self.sensor_shutter = QtWidgets.QSpinBox()
        self.sensor_shutter.setRange(1, 65531)
        self.sensor_shutter.setValue(args.sensor_shutter)

        self.sensor_gain16 = QtWidgets.QSpinBox()
        self.sensor_gain16.setRange(16, 1023)
        self.sensor_gain16.setValue(args.sensor_gain16)

        self.byte_order = QtWidgets.QComboBox()
        self.byte_order.addItems(["normal", "swap16"])
        self.byte_order.setCurrentText(args.byte_order)
        self.byte_order.setEnabled(False)

        panel = self.create_control_panel()
        central = QtWidgets.QWidget()
        layout = QtWidgets.QHBoxLayout(central)
        layout.setContentsMargins(10, 10, 10, 10)
        layout.setSpacing(10)
        layout.addWidget(self.video, 1)
        layout.addWidget(panel)
        self.setCentralWidget(central)

        self.receiver.frame_ready.connect(self.on_frame)
        self.receiver.status_changed.connect(self.status.setText)
        self.receiver.metrics_changed.connect(self.metrics.setText)
        self.receiver.connected_changed.connect(self.on_connection_changed)
        self.video.sample_requested.connect(self.on_pixel_sample)

        if not getattr(args, "defer_start", False):
            self.start_receiver()

    def start_receiver(self):
        if not self.receiver.isRunning():
            self.receiver.start()
        if self._initial_resolution_sent:
            return
        self._initial_resolution_sent = True
        initial_size = RESOLUTION_CHOICES[self.resolution.currentIndex()][1]
        if initial_size is not None:
            self.expected_resolution = initial_size
            self.current_epoch = self.receiver.request_resolution(initial_size)

    def create_control_panel(self):
        panel = QtWidgets.QWidget()
        panel.setFixedWidth(300)
        layout = QtWidgets.QVBoxLayout(panel)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(10)

        stream_group = QtWidgets.QGroupBox("Stream")
        form = QtWidgets.QFormLayout(stream_group)
        form.addRow("Resolution", self.resolution)
        form.addRow("Rotate", self.rotation)
        form.addRow("Byte order", self.byte_order)
        form.addRow("Status", self.status)
        form.addRow("Metrics", self.metrics)
        form.addRow("Pixel", self.pixel)
        self.resolution.currentIndexChanged.connect(self.on_resolution_changed)
        self.rotation.currentIndexChanged.connect(self.on_rotation_changed)
        layout.addWidget(stream_group)

        sensor_group = QtWidgets.QGroupBox("Sensor")
        sensor_form = QtWidgets.QFormLayout(sensor_group)
        sensor_form.addRow("Auto AE/AG", self.sensor_auto_ae)
        sensor_form.addRow("EV preset", self.sensor_ev)
        sensor_form.addRow("AE target", self.sensor_ae_target)
        sensor_form.addRow("Banding", self.sensor_banding)
        sensor_form.addRow("Shutter lines", self.sensor_shutter)
        sensor_form.addRow("Gain16", self.sensor_gain16)
        focus = QtWidgets.QPushButton("Focus")
        focus.clicked.connect(self.on_focus_clicked)
        sensor_form.addRow(focus)
        self.sensor_auto_ae.toggled.connect(self.on_sensor_auto_ae_changed)
        self.sensor_ev.currentIndexChanged.connect(self.on_sensor_ev_changed)
        self.sensor_ae_target.valueChanged.connect(self.on_sensor_ae_target_changed)
        self.sensor_banding.currentIndexChanged.connect(self.on_sensor_banding_changed)
        self.sensor_shutter.valueChanged.connect(self.on_sensor_shutter_changed)
        self.sensor_gain16.valueChanged.connect(self.on_sensor_gain16_changed)
        self.update_sensor_manual_enabled()
        layout.addWidget(sensor_group)

        picture_group = QtWidgets.QGroupBox("Picture")
        picture_layout = QtWidgets.QVBoxLayout(picture_group)
        self.sliders = {}
        self.add_slider(picture_layout, "r_gain", "R gain", 0, 200, self.args.r_gain)
        self.add_slider(picture_layout, "g_gain", "G gain", 0, 200, self.args.g_gain)
        self.add_slider(picture_layout, "b_gain", "B gain", 0, 200, self.args.b_gain)
        self.add_slider(picture_layout, "brightness", "Brightness", -100, 100, self.args.brightness)
        self.add_slider(picture_layout, "contrast", "Contrast", 1, 300, self.args.contrast)
        self.add_slider(picture_layout, "saturation", "Saturation", 0, 300, self.args.saturation)
        self.add_slider(picture_layout, "gamma", "Gamma", 10, 300, self.args.gamma)
        self.add_slider(picture_layout, "sharpness", "Sharpness", 0, 100, self.args.sharpness)
        layout.addWidget(picture_group)

        reset = QtWidgets.QPushButton("Reset Picture")
        reset.clicked.connect(self.reset_picture)
        layout.addWidget(reset)

        capture_group = QtWidgets.QGroupBox("Capture")
        capture_layout = QtWidgets.QVBoxLayout(capture_group)
        screenshot = QtWidgets.QPushButton("Screenshot")
        screenshot.clicked.connect(self.save_screenshot)
        self.record_button = QtWidgets.QPushButton("Start Recording")
        self.record_button.setCheckable(True)
        self.record_button.toggled.connect(self.toggle_recording)
        capture_layout.addWidget(screenshot)
        capture_layout.addWidget(self.record_button)
        layout.addWidget(capture_group)

        layout.addStretch(1)
        return panel

    def add_slider(self, layout, key, title, minimum, maximum, value):
        label = QtWidgets.QLabel(f"{title}: {value}")
        slider = QtWidgets.QSlider(QtCore.Qt.Horizontal)
        slider.setRange(minimum, maximum)
        slider.setValue(value)
        slider.valueChanged.connect(lambda v, k=key, l=label, t=title: self.on_slider_changed(k, l, t, v))
        layout.addWidget(label)
        layout.addWidget(slider)
        self.sliders[key] = slider

    def on_slider_changed(self, key, label, title, value):
        label.setText(f"{title}: {value}")
        self.update_picture_params()

    def update_picture_params(self):
        self.receiver.set_picture_params(**{key: slider.value() for key, slider in self.sliders.items()})

    def reset_picture(self):
        defaults = {
            "r_gain": 100,
            "g_gain": 100,
            "b_gain": 100,
            "brightness": 0,
            "contrast": 100,
            "saturation": 100,
            "gamma": 100,
            "sharpness": 0,
        }
        for key, value in defaults.items():
            self.sliders[key].setValue(value)
        self.update_picture_params()

    def on_resolution_changed(self, index):
        size = RESOLUTION_CHOICES[index][1]
        if size is not None:
            self.video.clear_image(f"Switching to {size[0]} x {size[1]}")
            self.metrics.setText("-")
            self.pixel.setText("-")
            self.last_frame_bgr = None
            if self.record_writer is not None:
                self.close_record_writer()
            self.expected_resolution = size
            self.current_epoch = self.receiver.request_resolution(size)

    def on_focus_clicked(self):
        self.receiver.request_focus()
        self.status.setText("focus requested")

    def on_rotation_changed(self, index):
        rotate = self.rotation.itemData(index)
        self.receiver.set_rotate(int(rotate))

    def on_sensor_ev_changed(self, index):
        ev = int(self.sensor_ev.itemData(index))
        self.receiver.request_sensor_ev(ev)
        self.status.setText(f"sensor ev {ev:+d}")

    def update_sensor_manual_enabled(self):
        manual = not self.sensor_auto_ae.isChecked()
        self.sensor_shutter.setEnabled(manual)
        self.sensor_gain16.setEnabled(manual)

    def on_sensor_auto_ae_changed(self, checked):
        self.update_sensor_manual_enabled()
        self.receiver.request_sensor_ae_ag(bool(checked))
        if not checked:
            self.receiver.request_sensor_shutter(self.sensor_shutter.value())
            self.receiver.request_sensor_gain16(self.sensor_gain16.value())
        self.status.setText("sensor ae/ag auto" if checked else "sensor ae/ag manual")

    def on_sensor_shutter_changed(self, value):
        if not self.sensor_auto_ae.isChecked():
            self.receiver.request_sensor_shutter(int(value))
            self.status.setText(f"sensor shutter {value}")

    def on_sensor_gain16_changed(self, value):
        if not self.sensor_auto_ae.isChecked():
            self.receiver.request_sensor_gain16(int(value))
            self.status.setText(f"sensor gain16 {value}")

    def on_sensor_ae_target_changed(self, value):
        self.receiver.request_sensor_ae_target(int(value))
        self.status.setText(f"sensor ae target {value}")

    def on_sensor_banding_changed(self, index):
        value = int(self.sensor_banding.itemData(index))
        self.receiver.request_sensor_banding(value)
        label = "auto" if value == 0 else f"{value} Hz"
        self.status.setText(f"sensor banding {label}")

    def enable_reconnect_dialog(self):
        self._reconnect_dialog_enabled = True

    def show_connection_dialog(self):
        if self._closing or self._reconnect_dialog_open:
            return QtWidgets.QDialog.Rejected
        self._reconnect_dialog_open = True
        dialog = ConnectionDialog(self.args, self.receiver, self)
        dialog.start()
        result = dialog.exec()
        self._reconnect_dialog_open = False
        return result

    def on_connection_changed(self, connected, text):
        if connected:
            return
        if self._closing or not self._reconnect_dialog_enabled:
            return
        self.video.clear_image("Waiting for camera stream")
        self.metrics.setText("-")
        self.pixel.setText("-")
        self.last_frame_bgr = None
        QtCore.QTimer.singleShot(100, self.show_connection_dialog)

    def on_frame(self, image, frame_bgr, width, height, _seq, epoch):
        if epoch != self.current_epoch:
            return
        if self.expected_resolution is not None and (width, height) != self.expected_resolution:
            return
        self.last_frame_bgr = frame_bgr.copy()
        self.write_record_frame(self.last_frame_bgr)
        self.video.set_image(image)
        self.status.setText(f"stream {width}x{height}")

    def output_dir(self):
        path = Path("captures")
        path.mkdir(exist_ok=True)
        return path

    def timestamp_name(self, prefix, suffix):
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        device_ip = getattr(self.args, "device_ip", None)
        if device_ip:
            safe_ip = device_ip.replace(".", "_").replace(":", "_")
            prefix = f"{prefix}_{safe_ip}_{self.args.port}"
        return self.output_dir() / f"{prefix}_{stamp}{suffix}"

    def save_screenshot(self):
        if self.last_frame_bgr is None:
            self.status.setText("no frame to save")
            return
        path = self.timestamp_name("camera", ".png")
        if cv2.imwrite(str(path), self.last_frame_bgr):
            self.status.setText(f"saved {path}")
        else:
            self.status.setText("screenshot failed")

    def toggle_recording(self, checked):
        self.recording = checked
        if checked:
            self.record_button.setText("Stop Recording")
            self.status.setText("recording armed")
        else:
            self.record_button.setText("Start Recording")
            self.close_record_writer()

    def open_record_writer(self, frame):
        height, width = frame.shape[:2]
        path = self.timestamp_name("camera", ".mp4")
        fourcc = cv2.VideoWriter_fourcc(*"mp4v")
        writer = cv2.VideoWriter(str(path), fourcc, 30.0, (width, height))
        if not writer.isOpened():
            self.status.setText("record open failed")
            return None
        self.record_path = path
        self.record_size = (width, height)
        self.status.setText(f"recording {path}")
        return writer

    def write_record_frame(self, frame):
        if not self.recording:
            return
        if self.record_writer is None:
            self.record_writer = self.open_record_writer(frame)
        if self.record_writer is None:
            return
        if (frame.shape[1], frame.shape[0]) != self.record_size:
            self.close_record_writer()
            self.record_writer = self.open_record_writer(frame)
            if self.record_writer is None:
                return
        self.record_writer.write(frame)

    def close_record_writer(self):
        if self.record_writer is not None:
            self.record_writer.release()
            self.record_writer = None
            self.record_size = None
            self.status.setText(f"saved {self.record_path}")

    def on_pixel_sample(self, x, y):
        if self.last_frame_bgr is None:
            return
        if y >= self.last_frame_bgr.shape[0] or x >= self.last_frame_bgr.shape[1]:
            return
        b, g, r = [int(v) for v in self.last_frame_bgr[y, x]]
        rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        self.pixel.setText(f"x={x} y={y} RGB=({r},{g},{b}) RGB565=0x{rgb565:04x}")

    def closeEvent(self, event):
        self._closing = True
        self.close_record_writer()
        self.receiver.stop()
        self.receiver.wait(1500)
        super().closeEvent(event)


def main():
    parser = argparse.ArgumentParser(description="Titan Mini RGB565 TCP viewer")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=9200)
    parser.add_argument("--resolution", type=parse_resolution, default=None,
                        help="initial board resolution: auto, 640x480, 320x240, or WIDTHxHEIGHT")
    parser.add_argument("--scale", type=float, default=0.5)
    parser.add_argument("--byte-order", choices=("normal", "swap16"), default="normal")
    parser.add_argument("--rotate", type=int, choices=(0, 90, 180, 270), default=180)
    parser.add_argument("--flip", choices=("none", "x", "y", "both"), default="none")
    parser.add_argument("--brightness", type=int, default=0)
    parser.add_argument("--contrast", type=int, default=100)
    parser.add_argument("--saturation", type=int, default=100)
    parser.add_argument("--gamma", type=int, default=100)
    parser.add_argument("--sharpness", type=int, default=0)
    parser.add_argument("--r-gain", dest="r_gain", type=int, default=100)
    parser.add_argument("--g-gain", dest="g_gain", type=int, default=100)
    parser.add_argument("--b-gain", dest="b_gain", type=int, default=100)
    parser.add_argument("--sensor-auto-ae", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--sensor-shutter", type=int, default=1000)
    parser.add_argument("--sensor-gain16", type=int, default=16)
    parser.add_argument("--sensor-ae-target", type=int, default=52)
    parser.add_argument("--sensor-banding", type=int, choices=(0, 50, 60), default=0)
    parser.add_argument("--stats", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--no-controls", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--no-display", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--mode", choices=("auto", "rgb565", "yuv422"), default="auto",
                        help=argparse.SUPPRESS)
    parser.add_argument("--order", choices=("yuyv", "uyvy", "yvyu", "vyuy"), default="yuyv",
                        help="YUV422 byte order for raw YUV2 frames")
    args = parser.parse_args()
    if args.mode not in ("auto", "rgb565", "yuv422"):
        raise SystemExit("only RGB565/YUV422 stream is supported")

    app = QtWidgets.QApplication([])
    window = MainWindow(args)
    window.show()
    app.exec()


if __name__ == "__main__":
    main()
