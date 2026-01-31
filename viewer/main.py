#!/usr/bin/env python3
"""
Input Overlay Recorder Viewer
A viewer to display video and IOR (Input Overlay Recording) files together

Features:
- Video playback with Qt Multimedia
- Automatic file pairing (loads .ior with video and vice versa)
- Key state visualization showing all active keys in one row
- Full event stream display with arrow pointing to current event
- Frame-by-frame navigation
- Event-based navigation
"""

import sys
from pathlib import Path

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QSplitter, QLabel, QPushButton, QSlider, QTableWidget, QTableWidgetItem,
    QFileDialog, QGroupBox, QHeaderView, QComboBox
)
from PyQt6.QtCore import Qt, QUrl
from PyQt6.QtMultimedia import QMediaPlayer, QAudioOutput
from PyQt6.QtMultimediaWidgets import QVideoWidget
from PyQt6.QtGui import QAction, QKeySequence, QShortcut


class IORParser:
    """Parser for IOR (Input Overlay Recording) files"""
    
    def __init__(self, filepath):
        self.filepath = filepath
        self.events = []
        self.start_time = 0
        self._parse()
    
    def _parse(self):
        """Parse the IOR file"""
        with open(self.filepath, 'r') as f:
            for line in f:
                parts = line.strip().split()
                if len(parts) >= 3:
                    timestamp_ms = int(parts[0])
                    event_type = parts[1]
                    key_code = int(parts[2])
                    
                    # Get start time from first event
                    if self.start_time == 0:
                        self.start_time = int(parts[3]) if len(parts) > 3 else 0
                    
                    self.events.append({
                        'timestamp_ms': timestamp_ms,
                        'event_type': event_type,
                        'key_code': key_code,
                        'start_time': int(parts[3]) if len(parts) > 3 else self.start_time,
                        'row_index': len(self.events)  # Store original row index
                    })
    
    def get_events_at_time(self, timestamp_ms):
        """Get events at a specific timestamp (within small window)"""
        return [e for e in self.events if abs(e['timestamp_ms'] - timestamp_ms) < 50]
    
    def find_next_event_after(self, timestamp_ms):
        """Find the next event after a given timestamp"""
        for event in self.events:
            if event['timestamp_ms'] > timestamp_ms:
                return event
        return None
    
    def find_prev_event_before(self, timestamp_ms):
        """Find the previous event before a given timestamp"""
        for event in reversed(self.events):
            if event['timestamp_ms'] < timestamp_ms:
                return event
        return None
    
    def get_current_event_row(self, timestamp_ms):
        """Find the current event row index for the given timestamp"""
        if not self.events:
            return None
        
        # Find the event that best matches current timestamp
        closest_event = None
        closest_distance = float('inf')
        
        for event in self.events:
            distance = abs(event['timestamp_ms'] - timestamp_ms)
            if distance < closest_distance:
                closest_distance = distance
                closest_event = event
        
        return closest_event['row_index'] if closest_event else None


class KeyStateWidget(QWidget):
    """Widget to display key state machine - shows all keys in a single row"""
    
    def __init__(self):
        super().__init__()
        self.current_keys = {}  # key_code -> key_name
        self.init_ui()
        self.setFixedHeight(120)  # Fixed height for consistent UI
    
    def init_ui(self):
        layout = QVBoxLayout()
        layout.setContentsMargins(8, 8, 8, 8)
        layout.setSpacing(6)

        title = QLabel("Key State Machine")
        title.setProperty("title", "title")
        layout.addWidget(title)

        # Device grouping (simplified - in real app would detect from IOR data)
        device_group = QGroupBox("Keyboard Device")
        device_layout = QVBoxLayout()
        device_layout.setContentsMargins(10, 10, 10, 10)
        device_layout.setSpacing(4)

        # Active keys string display - single row, no wrap
        self.active_keys_label = QLabel("keys pressed: (none)")
        self.active_keys_label.setWordWrap(False)
        self.active_keys_label.setAlignment(Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter)
        self.active_keys_label.setStyleSheet("""
            QLabel {
                padding: 12px;
                background-color: #2c3e50;
                border: 1px solid #34495e;
                border-radius: 6px;
                font-weight: 500;
                min-height: 40px;
                color: #ecf0f1;
                font-family: 'SF Mono', 'Monaco', 'Consolas', monospace;
                font-size: 13px;
            }
        """)
        device_layout.addWidget(self.active_keys_label)

        device_group.setLayout(device_layout)
        layout.addWidget(device_group)

        self.setLayout(layout)

        self.key_name_cache = {}
    
    def get_key_name(self, key_code):
        """Convert key code to readable name"""
        if key_code in self.key_name_cache:
            return self.key_name_cache[key_code]
        
        # Common key mappings
        key_map = {
            # Letters
            65: "A", 66: "B", 67: "C", 68: "D", 69: "E", 70: "F", 71: "G",
            72: "H", 73: "I", 74: "J", 75: "K", 76: "L", 77: "M", 78: "N",
            79: "O", 80: "P", 81: "Q", 82: "R", 83: "S", 84: "T", 85: "U",
            86: "V", 87: "W", 88: "X", 89: "Y", 90: "Z",
            # Numbers
            48: "0", 49: "1", 50: "2", 51: "3", 52: "4",
            53: "5", 54: "6", 55: "7", 56: "8", 57: "9",
            # Function keys
            112: "F1", 113: "F2", 114: "F3", 115: "F4", 116: "F5",
            117: "F6", 118: "F7", 119: "F8", 120: "F9", 121: "F10",
            122: "F11", 123: "F12",
            # Special keys
            8: "BACKSPACE", 9: "TAB", 13: "ENTER", 16: "SHIFT",
            17: "CTRL", 18: "ALT", 19: "PAUSE", 20: "CAPSLOCK",
            27: "ESC", 32: "SPACE", 33: "PAGEUP", 34: "PAGEDOWN",
            35: "END", 36: "HOME", 37: "LEFT", 38: "UP", 39: "RIGHT",
            40: "DOWN", 45: "INSERT", 46: "DELETE",
            # Numpad
            96: "NUM0", 97: "NUM1", 98: "NUM2", 99: "NUM3", 100: "NUM4",
            101: "NUM5", 102: "NUM6", 103: "NUM7", 104: "NUM8", 105: "NUM9",
            106: "NUM*", 107: "NUM+", 109: "NUM-", 110: "NUM.", 111: "NUM/",
            # Modifiers
            160: "LSHIFT", 161: "RSHIFT", 162: "LCTRL", 163: "RCTRL",
            164: "LALT", 165: "RALT",
        }
        
        name = key_map.get(key_code, f"KEY_{key_code}")
        self.key_name_cache[key_code] = name
        return name
    
    def update_key_state(self, events):
        """Update key state based on events"""
        for event in events:
            key_code = event['key_code']
            event_type = event['event_type']
            key_name = self.get_key_name(key_code)
            
            if event_type == "KEY_DOWN":
                self.current_keys[key_code] = key_name
            elif event_type == "KEY_UP":
                self.current_keys.pop(key_code, None)
        
        self.update_display()
    
    def update_display(self):
        """Update the visual display - single row format"""
        # Update active keys string - single row with comma separation
        if self.current_keys:
            active_names = sorted(self.current_keys.values())
            self.active_keys_label.setText(f"keys pressed: {', '.join(active_names)}")
            self.active_keys_label.setStyleSheet("""
                QLabel {
                    padding: 12px;
                    background-color: #2980b9;
                    border: 1px solid #1a5276;
                    border-radius: 6px;
                    font-weight: 500;
                    min-height: 40px;
                    color: #ffffff;
                    font-family: 'SF Mono', 'Monaco', 'Consolas', monospace;
                    font-size: 13px;
                }
            """)
        else:
            self.active_keys_label.setText("keys pressed: (none)")
            self.active_keys_label.setStyleSheet("""
                QLabel {
                    padding: 12px;
                    background-color: #34495e;
                    border: 1px solid #2c3e50;
                    border-radius: 6px;
                    font-weight: 500;
                    min-height: 40px;
                    color: #bdc3c7;
                    font-family: 'SF Mono', 'Monaco', 'Consolas', monospace;
                    font-size: 13px;
                }
            """)
    
    def reset(self):
        """Reset all key states"""
        self.current_keys.clear()
        self.update_display()


class EventStreamWidget(QWidget):
    """Widget to display event stream"""
    
    def __init__(self):
        super().__init__()
        self.current_row = None
        self.ior_events = []
        self.key_name_cache = {}
        self.last_prev_row = None
        self.last_next_row = None
        self.init_ui()
    
    def init_ui(self):
        layout = QVBoxLayout()
        layout.setContentsMargins(8, 8, 8, 8)
        layout.setSpacing(6)

        title = QLabel("Event Stream")
        title.setProperty("title", "title")
        layout.addWidget(title)

        self.table = QTableWidget()
        self.table.setColumnCount(5)
        self.table.setHorizontalHeaderLabels(["Time (ms)", "Type", "Key Code", "Key Name", "Current"])
        self.table.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Stretch)
        self.table.verticalHeader().setDefaultSectionSize(24)

        # Configure vertical header to show arrow
        self.table.verticalHeader().setVisible(True)
        self.table.verticalHeader().setFixedWidth(36)
        self.table.verticalHeader().setDefaultAlignment(Qt.AlignmentFlag.AlignCenter)

        layout.addWidget(self.table)

        self.setLayout(layout)
    
    def get_key_name(self, key_code):
        """Convert key code to readable name"""
        if key_code in self.key_name_cache:
            return self.key_name_cache[key_code]
        
        key_map = {
            65: "A", 66: "B", 67: "C", 68: "D", 69: "E", 70: "F", 71: "G",
            72: "H", 73: "I", 74: "J", 75: "K", 76: "L", 77: "M", 78: "N",
            79: "O", 80: "P", 81: "Q", 82: "R", 83: "S", 84: "T", 85: "U",
            86: "V", 87: "W", 88: "X", 89: "Y", 90: "Z",
            48: "0", 49: "1", 50: "2", 51: "3", 52: "4",
            53: "5", 54: "6", 55: "7", 56: "8", 57: "9",
            112: "F1", 113: "F2", 114: "F3", 115: "F4", 116: "F5",
            117: "F6", 118: "F7", 119: "F8", 120: "F9", 121: "F10",
            122: "F11", 123: "F12",
            8: "BACKSPACE", 9: "TAB", 13: "ENTER", 16: "SHIFT",
            17: "CTRL", 18: "ALT", 19: "PAUSE", 20: "CAPSLOCK",
            27: "ESC", 32: "SPACE", 33: "PAGEUP", 34: "PAGEDOWN",
            35: "END", 36: "HOME", 37: "LEFT", 38: "UP", 39: "RIGHT",
            40: "DOWN", 45: "INSERT", 46: "DELETE",
            96: "NUM0", 97: "NUM1", 98: "NUM2", 99: "NUM3", 100: "NUM4",
            101: "NUM5", 102: "NUM6", 103: "NUM7", 104: "NUM8", 105: "NUM9",
            106: "NUM*", 107: "NUM+", 109: "NUM-", 110: "NUM.", 111: "NUM/",
            160: "LSHIFT", 161: "RSHIFT", 162: "LCTRL", 163: "RCTRL",
            164: "LALT", 165: "RALT",
        }
        
        name = key_map.get(key_code, f"CODE_{key_code}")
        self.key_name_cache[key_code] = name
        return name
    
    def load_all_events(self, events):
        """Load all events from IOR file into table"""
        self.ior_events = events
        self.table.setRowCount(len(events))
        
        for i, event in enumerate(events):
            self.table.setItem(i, 0, QTableWidgetItem(str(event['timestamp_ms'])))
            self.table.setItem(i, 1, QTableWidgetItem(event['event_type']))
            self.table.setItem(i, 2, QTableWidgetItem(str(event['key_code'])))
            self.table.setItem(i, 3, QTableWidgetItem(self.get_key_name(event['key_code'])))
            self.table.setItem(i, 4, QTableWidgetItem(""))
            
            # Clear arrow for all rows initially
            self.table.setVerticalHeaderItem(i, QTableWidgetItem(""))
    
    def update_current_event(self, current_time_ms):
        """Update the current event indicator based on timestamp"""
        # Find last event before current time and next event after current time
        prev_row = -1  # -1 means before the first event
        next_row = -1  # -1 means after the last event

        for i, event in enumerate(self.ior_events):
            if event['timestamp_ms'] <= current_time_ms:
                prev_row = i
            else:
                next_row = i
                break

        # Clear previous highlights
        if self.last_prev_row is not None and self.last_prev_row >= 0 and self.last_prev_row < len(self.ior_events):
            self._clear_highlight(self.last_prev_row)
        if self.last_next_row is not None and self.last_next_row >= 0 and self.last_next_row < len(self.ior_events):
            self._clear_highlight(self.last_next_row)

        # Apply new highlights
        if prev_row >= 0 and prev_row < len(self.ior_events):
            self._apply_highlight(prev_row, current_time_ms, is_prev=True)
        if next_row >= 0 and next_row < len(self.ior_events):
            self._apply_highlight(next_row, current_time_ms, is_prev=False)

        # Store for next cleanup
        self.last_prev_row = prev_row
        self.last_next_row = next_row

        # Scroll to show the gap
        if prev_row >= 0 and prev_row < len(self.ior_events):
            self.table.scrollToItem(self.table.item(prev_row, 0), QTableWidget.ScrollHint.PositionAtCenter)
        elif next_row >= 0 and next_row < len(self.ior_events):
            self.table.scrollToItem(self.table.item(next_row, 0), QTableWidget.ScrollHint.PositionAtTop)
        elif len(self.ior_events) > 0:
            # Before all events, scroll to top
            self.table.scrollToItem(self.table.item(0, 0), QTableWidget.ScrollHint.PositionAtTop)
    
    def _clear_highlight(self, row):
        """Clear highlight from a row"""
        if row >= 0 and row < len(self.ior_events):
            for col in range(5):
                item = self.table.item(row, col)
                if item:
                    item.setBackground(Qt.GlobalColor.transparent)
                    item.setForeground(Qt.GlobalColor.transparent)
                    font = item.font()
                    font.setBold(False)
                    font.setFamily("SF Mono, Monaco, Consolas, monospace")
                    font.setPointSize(11)
                    item.setFont(font)

            self.table.setVerticalHeaderItem(row, QTableWidgetItem(""))

    def _apply_highlight(self, row, current_time_ms: int, is_prev: bool = True):
        """Apply highlight to a row"""
        if row >= 0 and row < len(self.ior_events):
            from PyQt6.QtGui import QColor, QBrush

            # Set arrow in vertical header
            if is_prev:
                # Previous event: gray background
                arrow_item = QTableWidgetItem("▼")
                bg_brush = QBrush(QColor(220, 220, 220))
                fg_color = Qt.GlobalColor.darkGray
            else:
                # Next event: orange background (more readable than yellow)
                arrow_item = QTableWidgetItem("▼")
                bg_brush = QBrush(QColor(255, 165, 0))  # Orange
                fg_color = Qt.GlobalColor.black

            arrow_item.setBackground(bg_brush)
            arrow_item.setForeground(fg_color)
            font = arrow_item.font()
            font.setBold(True)
            font.setFamily("SF Mono, Monaco, Consolas, monospace")
            font.setPointSize(11)
            arrow_item.setFont(font)
            self.table.setVerticalHeaderItem(row, arrow_item)

            # Highlight row background
            for col in range(5):
                item = self.table.item(row, col)
                if item:
                    item.setBackground(bg_brush)
                    item.setForeground(Qt.GlobalColor.black)
                    font = item.font()
                    font.setBold(True)
                    font.setFamily("SF Mono, Monaco, Consolas, monospace")
                    font.setPointSize(11)
                    item.setFont(font)

            # Update "Current" column with timestamp
            current_item = QTableWidgetItem(str(current_time_ms))
            current_item.setBackground(bg_brush)
            current_item.setForeground(fg_color)
            font = current_item.font()
            font.setBold(True)
            font.setFamily("SF Mono, Monaco, Consolas, monospace")
            font.setPointSize(11)
            current_item.setFont(font)
            self.table.setItem(row, 4, current_item)
    
    def clear(self):
        """Clear the event stream"""
        self.table.setRowCount(0)
        self.ior_events = []
        self.current_row = None


class ViewerWindow(QMainWindow):
    """Main viewer window"""

    # Unified UI styles - theme aware
    UNIFIED_STYLES = """
        /* Global settings */
        QWidget {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Arial, sans-serif;
            font-size: 12px;
        }

        /* Primary color palette - theme aware using palette() colors */
        QGroupBox {
            font-weight: 600;
            font-size: 13px;
            border: 1px solid palette(mid);
            border-radius: 6px;
            margin-top: 8px;
            padding-top: 10px;
            background-color: palette(alternate-base);
            color: palette(text);
        }

        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 10px;
            padding: 0 5px;
            color: palette(text);
        }

        /* Buttons */
        QPushButton {
            padding: 6px 14px;
            font-weight: 500;
            font-size: 12px;
            background-color: #3498db;
            color: white;
            border: none;
            border-radius: 4px;
            min-height: 20px;
        }

        QPushButton:hover {
            background-color: #2980b9;
        }

        QPushButton:pressed {
            background-color: #1a5276;
        }

        QPushButton:disabled {
            background-color: #bdc3c7;
            color: #7f8c8d;
        }

        /* Special button colors */
        QPushButton[text*="Stop"] {
            background-color: #e74c3c;
        }

        QPushButton[text*="Stop"]:hover {
            background-color: #c0392b;
        }

        /* Labels */
        QLabel {
            color: palette(text);
        }

        QLabel[title="title"] {
            font-weight: 600;
            font-size: 14px;
            color: palette(text);
        }

        /* Table Widget - theme aware */
        QTableWidget {
            border: 1px solid palette(mid);
            border-radius: 4px;
            background-color: palette(base);
            gridline-color: palette(midlight);
            selection-background-color: #3498db;
            selection-color: white;
            alternate-background-color: palette(alternate-base);
        }

        QTableWidget::item {
            padding: 4px;
            color: palette(text);
        }

        QTableWidget::item:selected {
            background-color: #3498db;
            color: white;
        }

        QHeaderView::section {
            background-color: palette(window);
            color: palette(text);
            font-weight: 600;
            padding: 5px;
            border: 1px solid palette(mid);
            border-right: none;
            border-bottom: none;
        }

        QHeaderView::section:last {
            border-right: 1px solid palette(mid);
        }

        /* Slider - theme aware */
        QSlider::groove:horizontal {
            height: 6px;
            background: palette(midlight);
            border-radius: 3px;
        }

        QSlider::handle:horizontal {
            width: 16px;
            height: 16px;
            background: #3498db;
            border: 2px solid palette(light);
            border-radius: 8px;
            margin: -5px 0;
        }

        QSlider::handle:horizontal:hover {
            background: #2980b9;
        }

        QSlider::add-page:horizontal {
            background: palette(midlight);
            border-radius: 3px;
        }

        QSlider::sub-page:horizontal {
            background: #3498db;
            border-radius: 3px;
        }

        /* ComboBox - theme aware */
        QComboBox {
            padding: 4px 8px;
            background-color: palette(base);
            border: 1px solid palette(mid);
            border-radius: 4px;
            min-width: 60px;
            color: palette(text);
        }

        QComboBox:hover {
            border-color: #3498db;
        }

        QComboBox::drop-down {
            border: none;
            width: 20px;
        }

        QComboBox::down-arrow {
            image: none;
            border-left: 5px solid transparent;
            border-right: 5px solid transparent;
            border-top: 5px solid palette(mid);
        }

        QComboBox QAbstractItemView {
            border: 1px solid palette(mid);
            background-color: palette(base);
            selection-background-color: #3498db;
            selection-color: white;
            padding: 2px;
            color: palette(text);
        }
    """

    def __init__(self):
        super().__init__()
        self.video_path = None
        self.ior_path = None
        self.ior_parser = None
        self.media_player = QMediaPlayer(self)
        self.audio_output = QAudioOutput(self)
        self.media_player.setAudioOutput(self.audio_output)

        # Apply unified styles
        self.setStyleSheet(self.UNIFIED_STYLES)

        self.init_ui()
        self.setup_connections()

    def closeEvent(self, event):
        """Handle window close event - ensure proper cleanup and exit"""
        # Stop playback
        self.media_player.stop()
        self.media_player.disconnect()

        # Delete media player and audio output
        if hasattr(self, 'audio_output'):
            self.audio_output.deleteLater()
        if hasattr(self, 'media_player'):
            self.media_player.deleteLater()

        event.accept()
    
    def init_ui(self):
        self.setWindowTitle("Input Overlay Recorder Viewer")
        self.setGeometry(100, 100, 1400, 800)

        # Create menu bar (keep for shortcuts but main buttons in UI)
        self.create_menu()

        # Main widget
        main_widget = QWidget()
        self.setCentralWidget(main_widget)

        # Main layout
        main_layout = QVBoxLayout()
        main_layout.setContentsMargins(12, 12, 12, 12)
        main_layout.setSpacing(8)
        main_widget.setLayout(main_layout)

        # File loading buttons (top row)
        load_layout = QHBoxLayout()
        load_layout.setSpacing(10)

        self.load_video_button = QPushButton("📹 Load Video...")
        self.load_video_button.setToolTip("Load video file (auto-loads .ior if present)")
        self.load_video_button.setStyleSheet("""
            QPushButton {
                padding: 8px 16px;
                font-weight: 600;
                font-size: 13px;
                background-color: #3498db;
                color: white;
                border: none;
                border-radius: 6px;
                min-height: 24px;
            }
            QPushButton:hover {
                background-color: #2980b9;
            }
            QPushButton:pressed {
                background-color: #1a5276;
            }
        """)

        self.load_ior_button = QPushButton("📄 Load IOR...")
        self.load_ior_button.setToolTip("Load IOR file (auto-loads video if present)")
        self.load_ior_button.setStyleSheet("""
            QPushButton {
                padding: 8px 16px;
                font-weight: 600;
                font-size: 13px;
                background-color: #27ae60;
                color: white;
                border: none;
                border-radius: 6px;
                min-height: 24px;
            }
            QPushButton:hover {
                background-color: #229954;
            }
            QPushButton:pressed {
                background-color: #1e8449;
            }
        """)

        load_layout.addWidget(self.load_video_button)
        load_layout.addWidget(self.load_ior_button)
        load_layout.addStretch()

        main_layout.addLayout(load_layout)

        # Splitter for left (video) and right (info)
        splitter = QSplitter(Qt.Orientation.Horizontal)
        splitter.setHandleWidth(1)
        splitter.setStyleSheet("""
            QSplitter::handle {
                background-color: #e0e0e0;
            }
            QSplitter::handle:hover {
                background-color: #3498db;
            }
        """)

        # Left side - Video
        left_widget = QWidget()
        left_layout = QVBoxLayout()
        left_layout.setContentsMargins(0, 0, 0, 0)
        left_layout.setSpacing(0)
        left_widget.setLayout(left_layout)

        self.video_widget = QVideoWidget()
        self.media_player.setVideoOutput(self.video_widget)
        self.video_widget.setMinimumSize(640, 480)
        self.video_widget.setStyleSheet("""
            QVideoWidget {
                background-color: palette(mid);
                border: 1px solid palette(mid);
                border-radius: 4px;
            }
        """)
        left_layout.addWidget(self.video_widget)

        splitter.addWidget(left_widget)

        # Right side - Info panel
        right_widget = QWidget()
        right_layout = QVBoxLayout()
        right_layout.setContentsMargins(0, 0, 0, 0)
        right_layout.setSpacing(8)
        right_widget.setLayout(right_layout)

        # Key state widget
        self.key_state_widget = KeyStateWidget()
        right_layout.addWidget(self.key_state_widget)

        # Event stream widget
        self.event_stream_widget = EventStreamWidget()
        right_layout.addWidget(self.event_stream_widget)

        splitter.addWidget(right_widget)
        splitter.setSizes([800, 400])

        main_layout.addWidget(splitter, 1)  # Give this stretch factor 1 (takes remaining space)

        # Controls
        controls_widget = QWidget()
        controls_widget.setMaximumHeight(36)
        controls_layout = QHBoxLayout()
        controls_layout.setContentsMargins(0, 0, 0, 0)
        controls_layout.setSpacing(6)

        self.play_pause_button = QPushButton("▶ Play")
        self.stop_button = QPushButton("⏹ Stop")
        self.prev_frame_button = QPushButton("⏮ Previous Frame")
        self.next_frame_button = QPushButton("⏭ Next Frame")
        self.prev_event_button = QPushButton("⏮ Previous Event")
        self.next_event_button = QPushButton("⏭ Next Event")

        # Speed control
        speed_label = QLabel("Speed:")
        speed_label.setStyleSheet("""
            QLabel {
                font-weight: 500;
                color: palette(text);
            }
        """)
        self.speed_combo = QComboBox()
        self.speed_combo.addItems(["0.25x", "0.5x", "0.75x", "1.0x", "1.25x", "1.5x", "2.0x", "3.0x", "4.0x"])
        self.speed_combo.setCurrentText("1.0x")
        self.speed_combo.setMaximumWidth(80)

        controls_layout.addWidget(self.play_pause_button)
        controls_layout.addWidget(self.stop_button)
        controls_layout.addWidget(self.prev_frame_button)
        controls_layout.addWidget(self.next_frame_button)
        controls_layout.addWidget(self.prev_event_button)
        controls_layout.addWidget(self.next_event_button)
        controls_layout.addWidget(speed_label)
        controls_layout.addWidget(self.speed_combo)

        controls_widget.setLayout(controls_layout)
        main_layout.addWidget(controls_widget, 0)  # No stretch (fixed height)

        # Progress bar
        progress_widget = QWidget()
        progress_widget.setMaximumHeight(32)
        progress_layout = QHBoxLayout()
        progress_layout.setContentsMargins(8, 0, 8, 0)
        progress_layout.setSpacing(8)

        self.progress_slider = QSlider(Qt.Orientation.Horizontal)
        self.progress_slider.setRange(0, 1000)
        self.progress_slider.setEnabled(False)
        self.progress_slider.setMaximumHeight(20)

        self.time_label = QLabel("0:00 / 0:00")
        self.time_label.setMinimumWidth(100)
        self.time_label.setStyleSheet("""
            QLabel {
                font-weight: 500;
                color: palette(text);
                font-family: 'SF Mono', 'Monaco', 'Consolas', monospace;
                font-size: 13px;
            }
        """)

        progress_layout.addWidget(self.progress_slider)
        progress_layout.addWidget(self.time_label)

        progress_widget.setLayout(progress_layout)
        main_layout.addWidget(progress_widget, 0)  # No stretch (fixed height)
        
        # Status bar
        self.statusBar().showMessage("Ready - Load a video or IOR file to begin")
    
    def create_menu(self):
        """Create menu bar"""
        menubar = self.menuBar()
        
        # File menu
        file_menu = menubar.addMenu("&File")
        
        load_video_action = QAction("Load &Video...", self)
        load_video_action.setShortcut("Ctrl+V")
        load_video_action.setStatusTip("Load video file (auto-loads .ior if present)")
        load_video_action.triggered.connect(self.load_video)
        file_menu.addAction(load_video_action)
        
        load_ior_action = QAction("Load &IOR File...", self)
        load_ior_action.setShortcut("Ctrl+I")
        load_ior_action.setStatusTip("Load IOR file (auto-loads video if present)")
        load_ior_action.triggered.connect(self.load_ior)
        file_menu.addAction(load_ior_action)
        
        file_menu.addSeparator()
        
        exit_action = QAction("&Exit", self)
        exit_action.setShortcut("Ctrl+Q")
        exit_action.triggered.connect(self.close)
        file_menu.addAction(exit_action)
    
    def setup_connections(self):
        """Setup signal connections"""
        # Load buttons
        self.load_video_button.clicked.connect(self.load_video)
        self.load_ior_button.clicked.connect(self.load_ior)

        # Playback controls
        self.play_pause_button.clicked.connect(self.toggle_play_pause)
        self.stop_button.clicked.connect(self.stop)

        self.prev_frame_button.clicked.connect(self.previous_frame)
        self.next_frame_button.clicked.connect(self.next_frame)

        self.prev_event_button.clicked.connect(self.previous_event)
        self.next_event_button.clicked.connect(self.next_event)

        # Speed control
        self.speed_combo.currentTextChanged.connect(self.change_speed)

        # Progress slider
        self.progress_slider.sliderPressed.connect(self.slider_pressed)
        self.progress_slider.sliderReleased.connect(self.slider_released)
        self.progress_slider.sliderMoved.connect(self.slider_moved)

        # Media player signals
        self.media_player.positionChanged.connect(self.position_changed)
        self.media_player.durationChanged.connect(self.duration_changed)

        # Keyboard shortcuts
        space_shortcut = QShortcut(QKeySequence(Qt.Key.Key_Space), self)
        space_shortcut.activated.connect(self.toggle_play_pause)
    
    def load_video(self):
        """Load video file"""
        file_path, _ = QFileDialog.getOpenFileName(
            self, "Load Video File", "", "Video Files (*.mp4 *.avi *.mkv *.mov);;All Files (*)"
        )
        
        if file_path:
            self.video_path = file_path
            self.media_player.setSource(QUrl.fromLocalFile(file_path))
            self.statusBar().showMessage(f"Loaded video: {Path(file_path).name}")
            
            # Try to load corresponding IOR file automatically
            video_path = Path(file_path)
            ior_path = video_path.with_suffix('.ior')
            
            if ior_path.exists():
                try:
                    self.ior_path = str(ior_path)
                    self.ior_parser = IORParser(str(ior_path))
                    self.statusBar().showMessage(f"Loaded video: {video_path.name} and IOR: {ior_path.name}")
                except Exception as e:
                    self.statusBar().showMessage(f"Loaded video: {video_path.name} (could not load IOR: {e})")
            else:
                self.statusBar().showMessage(f"Loaded video: {video_path.name} (no matching IOR file found)")
            
            # Show first frame and initialize display
            self.media_player.pause()  # Ensure paused
            QApplication.processEvents()  # Let media player load
            self.media_player.setPosition(0)
            
            # Initialize display
            self.check_ready()
            self.initialize_display()
    
    def load_ior(self):
        """Load IOR file"""
        file_path, _ = QFileDialog.getOpenFileName(
            self, "Load IOR File", "", "IOR Files (*.ior);;All Files (*)"
        )
        
        if file_path:
            self.ior_path = file_path
            self.ior_parser = IORParser(file_path)
            self.statusBar().showMessage(f"Loaded IOR file: {Path(file_path).name}")
            self.check_ready()

            # Try to load corresponding video file automatically
            ior_path = Path(file_path)
            for ext in ['.mp4', '.avi', '.mkv', '.mov']:
                video_path = ior_path.with_suffix(ext)
                if video_path.exists():
                    try:
                        self.video_path = str(video_path)
                        self.media_player.setSource(QUrl.fromLocalFile(str(video_path)))
                        self.statusBar().showMessage(f"Loaded IOR: {ior_path.name} and video: {video_path.name}")

                        # Show first frame
                        self.media_player.pause()
                        QApplication.processEvents()
                        self.media_player.setPosition(0)
                        self.initialize_display()
                        break
                    except Exception as e:
                        self.statusBar().showMessage(f"Loaded IOR: {ior_path.name} (could not load video: {e})")
                        break
            else:
                # No matching video found
                self.statusBar().showMessage(f"Loaded IOR file: {ior_path.name} (no matching video found)")
            
            # Initialize display (includes event loading)
            self.initialize_display()
    
    def check_ready(self):
        """Check if both video and IOR are loaded"""
        if self.video_path and self.ior_parser:
            self.progress_slider.setEnabled(True)
            self.statusBar().showMessage("Ready to play!")
    
    def initialize_display(self):
        """Initialize the display after loading files"""
        # Reset key states and event stream
        self.key_state_widget.reset()
        self.event_stream_widget.clear()
        
        # Load all events into event stream table
        if self.ior_parser:
            self.event_stream_widget.load_all_events(self.ior_parser.events)
            # Update event stream to show position 0
            self.event_stream_widget.update_current_event(0)
        
        # Update time label
        self.update_time_label(0)
        
        # Enable controls if both files loaded
        if self.video_path and self.ior_parser:
            self.progress_slider.setEnabled(True)
            self.statusBar().showMessage("Files loaded. Press Play to begin.")

    def toggle_play_pause(self):
        """Toggle between play and pause"""
        if self.media_player.playbackState() == QMediaPlayer.PlaybackState.PlayingState:
            self.media_player.pause()
        else:
            self.media_player.play()
        self.update_play_pause_button()

    def update_play_pause_button(self):
        """Update play/pause button text based on state"""
        if self.media_player.playbackState() == QMediaPlayer.PlaybackState.PlayingState:
            self.play_pause_button.setText("⏸ Pause")
        else:
            self.play_pause_button.setText("▶ Play")

    def change_speed(self, speed_text):
        """Change playback speed"""
        speed = float(speed_text.replace("x", ""))
        self.media_player.setPlaybackRate(speed)

    def stop(self):
        """Stop video"""
        self.media_player.stop()
        self.media_player.setPosition(0)
        self.key_state_widget.reset()
        self.event_stream_widget.clear()
    
    def previous_frame(self):
        """Go to previous frame"""
        current_pos = self.media_player.position()
        new_pos = max(0, current_pos - 33)  # ~30fps = 33ms per frame
        self.media_player.setPosition(new_pos)
    
    def next_frame(self):
        """Go to next frame"""
        current_pos = self.media_player.position()
        duration = self.media_player.duration()
        new_pos = min(duration, current_pos + 33)  # ~30fps = 33ms per frame
        self.media_player.setPosition(new_pos)
    
    def previous_event(self):
        """Go to previous event"""
        if self.ior_parser:
            current_pos = self.media_player.position()
            prev_event = self.ior_parser.find_prev_event_before(current_pos)
            if prev_event:
                self.media_player.setPosition(prev_event['timestamp_ms'])
    
    def next_event(self):
        """Go to next event"""
        if self.ior_parser:
            current_pos = self.media_player.position()
            next_event = self.ior_parser.find_next_event_after(current_pos)
            if next_event:
                self.media_player.setPosition(next_event['timestamp_ms'])
    
    def slider_pressed(self):
        """Handle slider press"""
        self.was_playing = self.media_player.playbackState() == QMediaPlayer.PlaybackState.PlayingState
        if self.was_playing:
            self.media_player.pause()

    def slider_released(self):
        """Handle slider release"""
        position = self.progress_slider.value() / 1000 * self.media_player.duration()
        self.media_player.setPosition(int(position))
        if self.was_playing:
            self.media_player.play()

    def slider_moved(self, value):
        """Handle slider move"""
        if self.media_player.duration() > 0:
            position = value / 1000 * self.media_player.duration()
            self.update_time_label(int(position))
            self.update_events(int(position))
            # Update video position immediately without waiting
            self.media_player.setPosition(int(position))
    
    def position_changed(self, position):
        """Handle position change"""
        if not self.progress_slider.isSliderDown():
            if self.media_player.duration() > 0:
                self.progress_slider.setValue(int(position / self.media_player.duration() * 1000))

        self.update_time_label(position)
        self.update_events(position)
        self.update_play_pause_button()
    
    def duration_changed(self, duration):
        """Handle duration change"""
        if duration > 0:
            self.progress_slider.setRange(0, 1000)
    
    def update_time_label(self, position):
        """Update time label"""
        duration = self.media_player.duration()
        current_time = self.format_time(position)
        total_time = self.format_time(duration)
        self.time_label.setText(f"{current_time} / {total_time}")
    
    def format_time(self, ms):
        """Format time in mm:ss"""
        seconds = ms // 1000
        minutes = seconds // 60
        seconds = seconds % 60
        return f"{minutes}:{seconds:02d}"
    
    def update_events(self, current_time_ms):
        """Update event display for current time"""
        if self.ior_parser:
            # For key state: calculate all events from start to current time
            self.key_state_widget.reset()
            for event in self.ior_parser.events:
                if event['timestamp_ms'] <= current_time_ms:
                    self.key_state_widget.update_key_state([event])
                else:
                    break
            
            # For event stream: show current event
            self.event_stream_widget.load_all_events(self.ior_parser.events)
            self.event_stream_widget.update_current_event(current_time_ms)


def main():
    app = QApplication(sys.argv)
    window = ViewerWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
