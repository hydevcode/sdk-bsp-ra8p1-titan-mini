#!/usr/bin/env python3
import argparse
from datetime import datetime
from pathlib import Path
import math
import socket
import time

import cv2
from PySide6 import QtCore, QtWidgets

from camera_yuv422_tcp_viewer import (
    BANDING_CHOICES,
    CameraReceiver,
    RESOLUTION_CHOICES,
    ROTATION_CHOICES,
    SENSOR_EV_CHOICES,
    VideoLabel,
    parse_resolution,
    resolution_index,
    rotation_index,
)


class PreviewTile(QtWidgets.QFrame):
    selected = QtCore.Signal(str)
    configure_requested = QtCore.Signal(str)
    close_requested = QtCore.Signal(str)
    status_reported = QtCore.Signal(str, str)

    def __init__(self, ip, args, parent=None):
        super().__init__(parent)
        self.ip = ip
        self.args = args
        self.receiver = CameraReceiver(args)
        self.current_epoch = 0
        self.expected_resolution = args.resolution
        self.last_frame_bgr = None
        self._initial_resolution_sent = False

        self.setFrameShape(QtWidgets.QFrame.StyledPanel)
        self.setProperty("selected", False)
        self.setStyleSheet(
            """
            PreviewTile {
                background: #0b0f14;
                border: 1px solid #29313a;
                border-radius: 4px;
            }
            PreviewTile[selected="true"] {
                border: 2px solid #2d7dd2;
            }
            QLabel {
                color: #c8d0d8;
            }
            """
        )

        self.title = QtWidgets.QLabel(f"{ip}  tcp:{args.port}")
        self.title.setStyleSheet("font-weight: 600;")
        self.status = QtWidgets.QLabel("Idle")
        self.metrics = QtWidgets.QLabel("-")

        self.video = VideoLabel()
        self.video.setMinimumSize(260, 180)

        header = QtWidgets.QHBoxLayout()
        header.addWidget(self.title)
        header.addStretch(1)
        header.addWidget(self.status)

        footer = QtWidgets.QHBoxLayout()
        footer.addWidget(self.metrics)
        footer.addStretch(1)

        layout = QtWidgets.QVBoxLayout(self)
        layout.setContentsMargins(6, 6, 6, 6)
        layout.setSpacing(6)
        layout.addLayout(header)
        layout.addWidget(self.video, 1)
        layout.addLayout(footer)

        self.receiver.frame_ready.connect(self.on_frame)
        self.receiver.status_changed.connect(self.on_status)
        self.receiver.metrics_changed.connect(self.metrics.setText)

    def set_selected(self, selected):
        self.setProperty("selected", bool(selected))
        self.style().unpolish(self)
        self.style().polish(self)

    def start(self):
        if not self.receiver.isRunning():
            self.receiver.start()
        self.request_initial_resolution()

    def request_initial_resolution(self):
        if self._initial_resolution_sent:
            return
        self._initial_resolution_sent = True
        if self.expected_resolution is not None:
            self.current_epoch = self.receiver.request_resolution(self.expected_resolution)

    def on_status(self, text):
        self.status.setText(text)
        self.status_reported.emit(self.ip, text)

    def on_frame(self, image, frame_bgr, width, height, _seq, epoch):
        if epoch != self.current_epoch:
            return
        if self.expected_resolution is not None and (width, height) != self.expected_resolution:
            return
        self.last_frame_bgr = frame_bgr.copy()
        self.video.set_image(image)
        self.status.setText(f"stream {width}x{height}")

    def apply_resolution(self, size):
        if size is None:
            return
        self.expected_resolution = size
        self.current_epoch = self.receiver.request_resolution(size)
        self.last_frame_bgr = None
        self.video.clear_image(f"Switching to {size[0]} x {size[1]}")

    def apply_rotation(self, rotate):
        self.receiver.set_rotate(int(rotate))

    def save_screenshot(self):
        if self.last_frame_bgr is None:
            self.status.setText("no frame")
            return
        path = Path("captures")
        path.mkdir(exist_ok=True)
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        safe_ip = self.ip.replace(".", "_").replace(":", "_")
        filename = path / f"camera_{safe_ip}_{self.args.port}_{stamp}.png"
        if cv2.imwrite(str(filename), self.last_frame_bgr):
            self.status.setText(f"saved {filename}")
        else:
            self.status.setText("save failed")

    def stop(self):
        self.receiver.stop()
        self.receiver.wait(1500)

    def mousePressEvent(self, event):
        self.selected.emit(self.ip)
        super().mousePressEvent(event)

    def contextMenuEvent(self, event):
        menu = QtWidgets.QMenu(self)
        config = menu.addAction("Configure")
        shot = menu.addAction("Screenshot")
        close = menu.addAction("Close")
        chosen = menu.exec(event.globalPos())
        if chosen == config:
            self.configure_requested.emit(self.ip)
        elif chosen == shot:
            self.save_screenshot()
        elif chosen == close:
            self.close_requested.emit(self.ip)


class TileConfigDialog(QtWidgets.QDialog):
    def __init__(self, tile, parent=None):
        super().__init__(parent)
        self.tile = tile
        self.setWindowTitle(f"Configure {tile.ip}")
        self.resize(360, 520)

        self.resolution = QtWidgets.QComboBox()
        for name, size in RESOLUTION_CHOICES:
            self.resolution.addItem(name, size)
        self.resolution.setCurrentIndex(resolution_index(tile.expected_resolution))

        self.rotation = QtWidgets.QComboBox()
        for name, rotate in ROTATION_CHOICES:
            self.rotation.addItem(name, rotate)
        self.rotation.setCurrentIndex(rotation_index(tile.receiver.rotate()))

        self.sensor_ev = QtWidgets.QComboBox()
        for name, ev in SENSOR_EV_CHOICES:
            self.sensor_ev.addItem(name, ev)
        self.sensor_ev.setCurrentIndex(2)

        self.sensor_auto_ae = QtWidgets.QCheckBox()
        self.sensor_auto_ae.setChecked(bool(tile.args.sensor_auto_ae))

        self.sensor_ae_target = QtWidgets.QSpinBox()
        self.sensor_ae_target.setRange(1, 255)
        self.sensor_ae_target.setValue(tile.args.sensor_ae_target)

        self.sensor_banding = QtWidgets.QComboBox()
        for name, value in BANDING_CHOICES:
            self.sensor_banding.addItem(name, value)
        for index in range(self.sensor_banding.count()):
            if self.sensor_banding.itemData(index) == tile.args.sensor_banding:
                self.sensor_banding.setCurrentIndex(index)
                break

        self.sensor_shutter = QtWidgets.QSpinBox()
        self.sensor_shutter.setRange(1, 65531)
        self.sensor_shutter.setValue(tile.args.sensor_shutter)

        self.sensor_gain16 = QtWidgets.QSpinBox()
        self.sensor_gain16.setRange(16, 1023)
        self.sensor_gain16.setValue(tile.args.sensor_gain16)

        params = tile.receiver.picture_params()
        self.sliders = {}

        form = QtWidgets.QFormLayout()
        form.addRow("Resolution", self.resolution)
        form.addRow("Rotate", self.rotation)
        form.addRow("Auto AE/AG", self.sensor_auto_ae)
        form.addRow("EV preset", self.sensor_ev)
        form.addRow("AE target", self.sensor_ae_target)
        form.addRow("Banding", self.sensor_banding)
        form.addRow("Shutter lines", self.sensor_shutter)
        form.addRow("Gain16", self.sensor_gain16)

        picture = QtWidgets.QGroupBox("Picture")
        picture_layout = QtWidgets.QVBoxLayout(picture)
        for key, title, minimum, maximum in (
            ("r_gain", "R gain", 0, 200),
            ("g_gain", "G gain", 0, 200),
            ("b_gain", "B gain", 0, 200),
            ("brightness", "Brightness", -100, 100),
            ("contrast", "Contrast", 1, 300),
            ("saturation", "Saturation", 0, 300),
            ("gamma", "Gamma", 10, 300),
            ("sharpness", "Sharpness", 0, 100),
        ):
            self.add_slider(picture_layout, key, title, minimum, maximum, params[key])

        reset_picture = QtWidgets.QPushButton("Reset Picture")
        picture_layout.addWidget(reset_picture)

        focus = QtWidgets.QPushButton("Focus")
        apply = QtWidgets.QPushButton("Apply")
        close = QtWidgets.QPushButton("Close")
        buttons = QtWidgets.QHBoxLayout()
        buttons.addWidget(focus)
        buttons.addStretch(1)
        buttons.addWidget(apply)
        buttons.addWidget(close)

        layout = QtWidgets.QVBoxLayout(self)
        layout.addLayout(form)
        layout.addWidget(picture)
        layout.addLayout(buttons)

        self.sensor_auto_ae.toggled.connect(self.update_sensor_manual_enabled)
        reset_picture.clicked.connect(self.reset_picture)
        focus.clicked.connect(tile.receiver.request_focus)
        apply.clicked.connect(self.apply_settings)
        close.clicked.connect(self.accept)
        self.update_sensor_manual_enabled()

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
        self.tile.receiver.set_picture_params(**{key: slider.value() for key, slider in self.sliders.items()})

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
        self.tile.receiver.set_picture_params(**defaults)

    def update_sensor_manual_enabled(self):
        manual = not self.sensor_auto_ae.isChecked()
        self.sensor_shutter.setEnabled(manual)
        self.sensor_gain16.setEnabled(manual)

    def apply_settings(self):
        size = self.resolution.currentData()
        if size is not None:
            self.tile.apply_resolution(size)
        self.tile.apply_rotation(self.rotation.currentData())

        self.tile.receiver.set_picture_params(
            **{key: slider.value() for key, slider in self.sliders.items()}
        )

        auto_ae = self.sensor_auto_ae.isChecked()
        self.tile.receiver.request_sensor_ae_ag(auto_ae)
        self.tile.receiver.request_sensor_ev(int(self.sensor_ev.currentData()))
        self.tile.receiver.request_sensor_ae_target(self.sensor_ae_target.value())
        self.tile.receiver.request_sensor_banding(int(self.sensor_banding.currentData()))
        if not auto_ae:
            self.tile.receiver.request_sensor_shutter(self.sensor_shutter.value())
            self.tile.receiver.request_sensor_gain16(self.sensor_gain16.value())


class MultiCameraWindow(QtWidgets.QMainWindow):
    """Single-window viewer: one listening tile, shows the stream when the camera client connects."""

    def __init__(self, args):
        super().__init__()
        self.args = args
        self._closing = False

        self.setWindowTitle("Titan Camera Viewer")
        self.resize(860, 640)

        self.tile = PreviewTile("camera", self.make_tile_args(), self)
        self.tile.configure_requested.connect(self.configure_tile)
        self.tile.close_requested.connect(self.close)
        self.tile.status_reported.connect(lambda _ip, text: self.status.setText(text))

        self.create_toolbar()

        self.status = QtWidgets.QLabel(
            "Listening on {0}:{1}, waiting for camera client connection...".format(args.host, args.port)
        )
        central = QtWidgets.QWidget()
        layout = QtWidgets.QVBoxLayout(central)
        layout.setContentsMargins(6, 6, 6, 6)
        layout.addWidget(self.tile, 1)
        layout.addWidget(self.status)
        self.setCentralWidget(central)

        self.tile.start()

    def make_tile_args(self):
        tile_args = argparse.Namespace(**vars(self.args))
        tile_args.device_ip = "camera"
        return tile_args

    def create_toolbar(self):
        toolbar = QtWidgets.QToolBar("Tools")
        toolbar.setMovable(False)
        self.addToolBar(toolbar)
        self.config_action = toolbar.addAction("Configure")
        self.shot_action = toolbar.addAction("Screenshot")
        self.config_action.triggered.connect(self.configure_tile)
        self.shot_action.triggered.connect(self.screenshot)

    def configure_tile(self, *_args):
        dialog = TileConfigDialog(self.tile, self)
        dialog.exec()

    def screenshot(self):
        self.tile.save_screenshot()

    def closeEvent(self, event):
        if getattr(self, "_closing", False):
            super().closeEvent(event)
            return
        self._closing = True
        self.tile.stop()
        super().closeEvent(event)


def main():
    parser = argparse.ArgumentParser(description="Titan multi RGB565 TCP viewer")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=9200)
    parser.add_argument("--resolution", type=parse_resolution, default=(640, 480))
    parser.add_argument("--scale", type=float, default=0.5)
    parser.add_argument("--mode", choices=("auto", "rgb565", "yuv422"), default="auto",
                        help=argparse.SUPPRESS)
    parser.add_argument("--byte-order", choices=("normal", "swap16"), default="normal")
    parser.add_argument("--order", choices=("yuyv", "uyvy", "yvyu", "vyuy"), default="yuyv",
                        help="YUV422 byte order for raw YUV2 frames")
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
    args = parser.parse_args()

    app = QtWidgets.QApplication([])
    window = MultiCameraWindow(args)
    window.show()
    app.exec()


if __name__ == "__main__":
    main()
