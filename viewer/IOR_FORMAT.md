# IOR File Format Specification

## Overview

The `.ior` (Input Overlay Recording) file format is a text-based format for recording input events from keyboard and mouse devices. Each line in the file represents a single event with timing information.

## File Structure

### General Format

```
<timestamp> <event_type> [event_data...]
```

All lines are terminated with `\r\n` (Windows line ending).

### Header Line

The first line of every IOR file is a special START event:

```
0 START <epoch_milliseconds>
```

- **timestamp**: Always `0` (recording start time)
- **event_type**: Always `START`
- **epoch_milliseconds**: Unix epoch timestamp in milliseconds (e.g., `1738156708301`)

This header stores the absolute Unix epoch time when recording began, for synchronization with external events.

## Event Types

### 1. KEY_DOWN

Records keyboard key press events.

**Format:**
```
<timestamp> KEY_DOWN <keycode>
```

**Fields:**
- `timestamp`: Time in milliseconds since recording started
- `keycode`: Virtual key code (integer)

**Example:**
```
1179 KEY_DOWN 87
```

**Common Key Codes:**
- `87` = W
- `65` = A
- `83` = S
- `68` = D

More key codes can be found in the uiohook.h file.

### 2. KEY_UP

Records keyboard key release events.

**Format:**
```
<timestamp> KEY_UP <keycode>
```

**Fields:**
- Same as KEY_DOWN

**Example:**
```
2332 KEY_UP 65
```

### 3. PAUSE / RESUME

Record obs pause and resume events.


**Format:**
```
<timestamp> PAUSE
<timestamp> RESUME
```

**Example:**
```
2332 PAUSE
3332 RESUME
```

It means user paused 1 second then resume recording.
There might be other events happen during the pause duration, this is to avoid key up/down event missing.

## Timestamp Format

All timestamps (except the START event) are integers representing milliseconds elapsed since recording started, pause/resume won't affect this, ior always collect events even if recording is paused.
You can checkout the viewer example app for this logic.

**Format:** `MMMMMM`
- Milliseconds: Variable length integer

**Examples:**
- `3` = 3 milliseconds (0.003 seconds)
- `1179` = 1179 milliseconds (1.179 seconds)
- `23123` = 23123 milliseconds (23.123 seconds)

## Event Ordering

Events are recorded in chronological order based on their timestamps. Multiple events can occur at very similar timestamps (within milliseconds of each other).

**Example:**
```
1179 KEY_DOWN 87
1180 MOUSE_MOVE 3478 509 26 6
```

(These events occur 1 millisecond apart)

## Sample File Structure

```
0 START 1738156708301
3 MOUSE_MOVE 6070 1628 -26 -6
11 MOUSE_MOVE 6023 1615 -36 -10
597 MOUSE_PRESS 1 3238 600
642 MOUSE_RELEASE 1 3238 600
1179 KEY_DOWN 87
2332 KEY_UP 65
```

note that MOUSE part is still a beta feature because it is not required by user, it's turned off by default. If you want to try it, you need to switch it on in setting manually.