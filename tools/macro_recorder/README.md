# Macro Recorder

A small desktop automation utility for productivity tasks — form filling,
repetitive data entry, and click-through application testing. Records mouse
and keyboard input to an editable XML script and replays it later.

## What it does

- **Records** mouse moves/clicks/scrolls and key presses/releases while
  recording is active, with millisecond timing.
- **Saves** each recording as a human-editable XML file under `scripts/`.
- **Replays** a saved script by re-generating the same input sequence via
  normal OS-level synthetic input (the same mechanism accessibility and
  testing tools use) — it does not read or write any other process's
  memory, does not inject into any window, and makes no network calls.
- **Hotkey driven**, with a small always-on-top status window showing the
  current state and script name.

## Foreground-window safety guard

Every recording stores the title of the window that was focused when
recording started (`target_window` in the XML). Before playback starts, and
periodically during playback, the tool checks the current foreground window:

- If it doesn't match the recorded target, playback **refuses to start**.
- If the user switches windows **during** playback, it **aborts
  immediately**.

This keeps a script scoped to the window it was made for — it will not fire
into whatever window happens to be focused later. (On platforms where the
foreground window can't be detected — e.g. Linux without `xdotool`
installed — this check is skipped and a plain warning is not currently
printed; treat that as "unenforced" on those setups.)

## Install

```bash
pip install -r requirements.txt
```

Platform notes:
- **Windows**: works out of the box.
- **macOS**: grant the terminal/Python **Accessibility** and **Input
  Monitoring** permissions (System Settings → Privacy & Security), or
  global mouse/keyboard hooking will silently fail.
- **Linux**: requires X11 (Wayland is not well supported by global input
  hooking libraries). Install `xdotool` for the foreground-window safety
  check: `sudo apt install xdotool` (or equivalent).

## Usage

```bash
python3 macro_recorder.py --name fill_form
```

A small status window appears. Hotkeys (global, while the tool is running):

| Key | Action |
|-----|--------|
| `F9`  | Start / stop recording |
| `F10` | Start / stop playback |
| `F11` | Abort current recording (discarded) or playback |

The script is saved to `scripts/<name>.xml` when recording stops.

List saved scripts:

```bash
python3 macro_recorder.py --list
```

## XML format

```xml
<macro name="fill_form" target_window="My App - Data Entry" created_at="2026-08-29T00:00:00+00:00">
  <event type="mouse_move" offset_ms="0" x="400" y="300"/>
  <event type="mouse_click" offset_ms="120" x="400" y="300" button="left" pressed="True"/>
  <event type="mouse_click" offset_ms="180" x="400" y="300" button="left" pressed="False"/>
  <event type="key_press" offset_ms="400" key="char:a"/>
  <event type="key_release" offset_ms="460" key="char:a"/>
</macro>
```

`offset_ms` is milliseconds since recording started. Keys are encoded as
`char:<c>` for character keys or `special:<name>` for named keys (e.g.
`special:enter`, `special:tab`, `special:shift`). The file is plain text —
edit offsets, remove events, or hand-author new ones as needed.

## Scope

- Operates only against the current foreground window (see safety guard
  above) — it does not target or interact with background processes.
- Makes no network requests.
- Mouse-move sampling is throttled (~33/sec) to keep scripts small and
  readable; clicks, scrolls, and key events are always captured in full.
