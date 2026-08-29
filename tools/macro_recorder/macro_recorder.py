#!/usr/bin/env python3
"""
Desktop macro recorder/player for productivity tasks (form filling, data
entry, application testing).

- Records mouse and keyboard sequences to a human-editable XML script.
- Hotkey-triggered: F9 toggles recording, F10 toggles playback, F11 aborts.
- Small always-on-top status window shows current state and script name.
- Safety guard: each script remembers the foreground window title it was
  recorded against. Playback checks the current foreground window before
  starting and aborts immediately if the user switches windows mid-playback,
  so a script only ever drives the window it was made for.
- Does not touch any other process's memory, does not inject into any
  window, and makes no network calls. Input is generated with normal
  OS-level synthetic input APIs (the same mechanism used by accessibility
  tools), targeted at whatever window currently has focus.

Usage:
    python3 macro_recorder.py --name my_script

Hotkeys (global, while the tool is running):
    F9   - start/stop recording
    F10  - start/stop playback
    F11  - abort current recording or playback immediately
"""

import argparse
import os
import subprocess
import sys
import threading
import time
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Dict, List, Optional
from xml.dom import minidom

try:
    from pynput import keyboard, mouse
except ImportError:
    print(
        "error: this tool requires the 'pynput' package.\n"
        "Install it with: pip install pynput",
        file=sys.stderr,
    )
    raise

SCRIPTS_DIR_DEFAULT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "scripts")
MOVE_THROTTLE_S = 0.03  # minimum time between recorded mouse-move samples

HOTKEY_RECORD = keyboard.Key.f9
HOTKEY_PLAY = keyboard.Key.f10
HOTKEY_ABORT = keyboard.Key.f11


# ==================== Foreground window detection (best-effort) ====================

def get_foreground_window_title() -> Optional[str]:
    """Best-effort, read-only foreground-window title lookup. Returns None if
    unavailable on this platform/environment (safety check is then skipped)."""
    try:
        if sys.platform.startswith("win"):
            import ctypes

            hwnd = ctypes.windll.user32.GetForegroundWindow()
            length = ctypes.windll.user32.GetWindowTextLengthW(hwnd)
            buf = ctypes.create_unicode_buffer(length + 1)
            ctypes.windll.user32.GetWindowTextW(hwnd, buf, length + 1)
            return buf.value or None

        if sys.platform == "darwin":
            script = (
                'tell application "System Events" to get name of first '
                "application process whose frontmost is true"
            )
            out = subprocess.run(
                ["osascript", "-e", script], capture_output=True, text=True, timeout=1.0
            )
            return out.stdout.strip() or None

        # Linux / X11 best-effort via xdotool, if present.
        out = subprocess.run(
            ["xdotool", "getactivewindow", "getwindowname"],
            capture_output=True,
            text=True,
            timeout=1.0,
        )
        if out.returncode == 0:
            return out.stdout.strip() or None
        return None
    except Exception:
        return None


# ==================== Macro data model ====================

@dataclass
class MacroEvent:
    type: str  # mouse_move | mouse_click | mouse_scroll | key_press | key_release
    offset_ms: int
    attrs: Dict[str, str] = field(default_factory=dict)


@dataclass
class MacroScript:
    name: str
    target_window: Optional[str]
    created_at: str
    events: List[MacroEvent]

    def save(self, path: str) -> None:
        root = ET.Element("macro", {
            "name": self.name,
            "target_window": self.target_window or "",
            "created_at": self.created_at,
        })
        for ev in self.events:
            el = ET.SubElement(root, "event", {
                "type": ev.type,
                "offset_ms": str(ev.offset_ms),
                **ev.attrs,
            })
        rough = ET.tostring(root, encoding="unicode")
        pretty = minidom.parseString(rough).toprettyxml(indent="  ")
        with open(path, "w", encoding="utf-8") as f:
            f.write(pretty)

    @staticmethod
    def load(path: str) -> "MacroScript":
        tree = ET.parse(path)
        root = tree.getroot()
        events = []
        for el in root.findall("event"):
            attrs = dict(el.attrib)
            ev_type = attrs.pop("type")
            offset_ms = int(attrs.pop("offset_ms"))
            events.append(MacroEvent(type=ev_type, offset_ms=offset_ms, attrs=attrs))
        return MacroScript(
            name=root.attrib.get("name", "untitled"),
            target_window=root.attrib.get("target_window") or None,
            created_at=root.attrib.get("created_at", ""),
            events=events,
        )


# ==================== Key (de)serialization ====================

def key_to_str(key) -> str:
    if isinstance(key, keyboard.KeyCode):
        if key.char is not None:
            return f"char:{key.char}"
        return f"vk:{key.vk}"
    return f"special:{key.name}"


def str_to_key(s: str):
    if s.startswith("char:"):
        return keyboard.KeyCode.from_char(s[len("char:"):])
    if s.startswith("vk:"):
        return keyboard.KeyCode.from_vk(int(s[len("vk:"):]))
    if s.startswith("special:"):
        return getattr(keyboard.Key, s[len("special:"):])
    raise ValueError(f"unrecognized key encoding: {s!r}")


# ==================== Shared UI state ====================

class AppState:
    def __init__(self):
        self.mode = "idle"  # idle | recording | playing
        self.script_name = "untitled"
        self.detail = ""
        self.lock = threading.Lock()

    def set(self, mode: Optional[str] = None, script_name: Optional[str] = None, detail: Optional[str] = None):
        with self.lock:
            if mode is not None:
                self.mode = mode
            if script_name is not None:
                self.script_name = script_name
            if detail is not None:
                self.detail = detail

    def snapshot(self):
        with self.lock:
            return self.mode, self.script_name, self.detail


# ==================== Recorder ====================

class Recorder:
    def __init__(self, state: AppState, scripts_dir: str):
        self.state = state
        self.scripts_dir = scripts_dir
        self.recording = False
        self.events: List[MacroEvent] = []
        self.start_time = 0.0
        self.last_move_time = 0.0
        self.target_window: Optional[str] = None

    def start(self, name: str):
        if self.recording:
            return
        self.events = []
        self.start_time = time.perf_counter()
        self.last_move_time = 0.0
        self.target_window = get_foreground_window_title()
        self.recording = True
        self.state.set(mode="recording", script_name=name,
                        detail=f"target: {self.target_window or 'unknown'}")
        print(f"[recording] started for window: {self.target_window!r}")

    def stop(self) -> Optional[MacroScript]:
        if not self.recording:
            return None
        self.recording = False
        script = MacroScript(
            name=self.state.snapshot()[1],
            target_window=self.target_window,
            created_at=datetime.now(timezone.utc).isoformat(),
            events=self.events,
        )
        self.state.set(mode="idle", detail=f"{len(self.events)} events recorded")
        print(f"[recording] stopped, {len(self.events)} events captured")
        return script

    def _offset_ms(self) -> int:
        return int((time.perf_counter() - self.start_time) * 1000)

    # --- mouse callbacks ---
    def on_move(self, x, y):
        if not self.recording:
            return
        now = time.perf_counter()
        if now - self.last_move_time < MOVE_THROTTLE_S:
            return
        self.last_move_time = now
        self.events.append(MacroEvent("mouse_move", self._offset_ms(), {"x": str(x), "y": str(y)}))

    def on_click(self, x, y, button, pressed):
        if not self.recording:
            return
        self.events.append(MacroEvent(
            "mouse_click", self._offset_ms(),
            {"x": str(x), "y": str(y), "button": str(button).split(".")[-1], "pressed": str(pressed)},
        ))

    def on_scroll(self, x, y, dx, dy):
        if not self.recording:
            return
        self.events.append(MacroEvent(
            "mouse_scroll", self._offset_ms(),
            {"x": str(x), "y": str(y), "dx": str(dx), "dy": str(dy)},
        ))

    # --- keyboard callbacks (hotkeys are filtered out before reaching here) ---
    def on_key_press(self, key):
        if not self.recording:
            return
        self.events.append(MacroEvent("key_press", self._offset_ms(), {"key": key_to_str(key)}))

    def on_key_release(self, key):
        if not self.recording:
            return
        self.events.append(MacroEvent("key_release", self._offset_ms(), {"key": key_to_str(key)}))


# ==================== Player ====================

class Player:
    def __init__(self, state: AppState):
        self.state = state
        self.playing = False
        self._abort = False
        self._thread: Optional[threading.Thread] = None
        self.mouse_ctl = mouse.Controller()
        self.keyboard_ctl = keyboard.Controller()

    def start(self, script: MacroScript):
        if self.playing:
            return

        current = get_foreground_window_title()
        if script.target_window and current is not None and current != script.target_window:
            print(
                f"[playback] refused: recorded for {script.target_window!r}, "
                f"current foreground window is {current!r}"
            )
            self.state.set(detail="blocked: foreground window mismatch")
            return

        self._abort = False
        self.playing = True
        self.state.set(mode="playing", script_name=script.name, detail="0 / %d events" % len(script.events))
        self._thread = threading.Thread(target=self._run, args=(script,), daemon=True)
        self._thread.start()

    def abort(self):
        self._abort = True

    def _run(self, script: MacroScript):
        last_offset = 0
        for i, ev in enumerate(script.events):
            if self._abort:
                print("[playback] aborted")
                break

            # Safety: re-check foreground window hasn't changed mid-playback.
            if script.target_window:
                current = get_foreground_window_title()
                if current is not None and current != script.target_window:
                    print(
                        f"[playback] aborted: foreground window changed to {current!r} "
                        f"(expected {script.target_window!r})"
                    )
                    break

            delay = max(0, ev.offset_ms - last_offset) / 1000.0
            if delay > 0:
                time.sleep(delay)
            last_offset = ev.offset_ms

            self._dispatch(ev)
            self.state.set(detail=f"{i + 1} / {len(script.events)} events")

        self.playing = False
        self.state.set(mode="idle")

    def _dispatch(self, ev: MacroEvent):
        a = ev.attrs
        if ev.type == "mouse_move":
            self.mouse_ctl.position = (int(a["x"]), int(a["y"]))
        elif ev.type == "mouse_click":
            self.mouse_ctl.position = (int(a["x"]), int(a["y"]))
            button = getattr(mouse.Button, a["button"])
            if a["pressed"] == "True":
                self.mouse_ctl.press(button)
            else:
                self.mouse_ctl.release(button)
        elif ev.type == "mouse_scroll":
            self.mouse_ctl.scroll(int(a["dx"]), int(a["dy"]))
        elif ev.type == "key_press":
            self.keyboard_ctl.press(str_to_key(a["key"]))
        elif ev.type == "key_release":
            self.keyboard_ctl.release(str_to_key(a["key"]))


# ==================== Status window ====================

class StatusWindow:
    def __init__(self, state: AppState):
        import tkinter as tk
        self.tk = tk
        self.state = state

        self.root = tk.Tk()
        self.root.title("Macro Recorder")
        self.root.geometry("260x90+40+40")
        self.root.attributes("-topmost", True)
        self.root.resizable(False, False)

        self.mode_var = tk.StringVar(value="Idle")
        self.name_var = tk.StringVar(value="untitled")
        self.detail_var = tk.StringVar(value="F9 record  |  F10 play  |  F11 abort")

        tk.Label(self.root, textvariable=self.mode_var, font=("Segoe UI", 14, "bold")).pack(pady=(8, 0))
        tk.Label(self.root, textvariable=self.name_var, font=("Segoe UI", 10)).pack()
        tk.Label(self.root, textvariable=self.detail_var, font=("Segoe UI", 8), wraplength=240).pack(pady=(4, 0))

        self._poll()

    def _poll(self):
        mode, script_name, detail = self.state.snapshot()
        self.mode_var.set(mode.capitalize())
        self.name_var.set(script_name)
        self.detail_var.set(detail or "F9 record  |  F10 play  |  F11 abort")
        self.root.after(150, self._poll)

    def run(self):
        self.root.mainloop()


# ==================== Wiring ====================

class MacroApp:
    def __init__(self, script_name: str, scripts_dir: str):
        self.scripts_dir = scripts_dir
        os.makedirs(scripts_dir, exist_ok=True)
        self.script_path = os.path.join(scripts_dir, f"{script_name}.xml")

        self.state = AppState()
        self.state.set(mode="idle", script_name=script_name, detail="F9 record | F10 play | F11 abort")
        self.recorder = Recorder(self.state, scripts_dir)
        self.player = Player(self.state)
        self.script_name = script_name

        self.mouse_listener = mouse.Listener(
            on_move=self.recorder.on_move,
            on_click=self.recorder.on_click,
            on_scroll=self.recorder.on_scroll,
        )
        self.keyboard_listener = keyboard.Listener(
            on_press=self._on_key_press,
            on_release=self._on_key_release,
        )

    def _on_key_press(self, key):
        if key == HOTKEY_RECORD:
            self.toggle_record()
            return
        if key == HOTKEY_PLAY:
            self.toggle_play()
            return
        if key == HOTKEY_ABORT:
            self.abort()
            return
        self.recorder.on_key_press(key)

    def _on_key_release(self, key):
        if key in (HOTKEY_RECORD, HOTKEY_PLAY, HOTKEY_ABORT):
            return
        self.recorder.on_key_release(key)

    def toggle_record(self):
        if self.player.playing:
            return
        if self.recorder.recording:
            script = self.recorder.stop()
            if script:
                script.save(self.script_path)
                print(f"[saved] {self.script_path}")
        else:
            self.recorder.start(self.script_name)

    def toggle_play(self):
        if self.recorder.recording:
            return
        if self.player.playing:
            self.player.abort()
            return
        if not os.path.isfile(self.script_path):
            print(f"[playback] no saved script at {self.script_path} yet — record one first (F9)")
            return
        script = MacroScript.load(self.script_path)
        self.player.start(script)

    def abort(self):
        if self.recorder.recording:
            self.recorder.recording = False
            self.state.set(mode="idle", detail="recording aborted (not saved)")
            print("[recording] aborted, not saved")
        if self.player.playing:
            self.player.abort()

    def run(self):
        self.mouse_listener.start()
        self.keyboard_listener.start()
        try:
            StatusWindow(self.state).run()
        finally:
            self.mouse_listener.stop()
            self.keyboard_listener.stop()


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Desktop macro recorder/player for productivity tasks.")
    parser.add_argument("--name", default="untitled", help="Script name (also the XML filename).")
    parser.add_argument("--scripts-dir", default=SCRIPTS_DIR_DEFAULT, help="Directory to save/load scripts from.")
    parser.add_argument("--list", action="store_true", help="List saved scripts and exit.")
    args = parser.parse_args(argv)

    if args.list:
        os.makedirs(args.scripts_dir, exist_ok=True)
        for fn in sorted(os.listdir(args.scripts_dir)):
            if fn.endswith(".xml"):
                print(fn[:-4])
        return 0

    app = MacroApp(args.name, args.scripts_dir)
    print(f"Ready. Script: {args.name}  (saved to {app.script_path})")
    print("F9 = record/stop, F10 = play/stop, F11 = abort")
    app.run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
