# Event Recorder - Quick Start Guide

## What is Event Recorder?

Event Recorder is a new feature that automatically records all your keyboard, mouse, and gamepad inputs while OBS is recording. The recorded data is saved to a `.ior` file that can be used for replay, analysis, or automation.

## Key Features

✅ **Automatic**: Starts/stops with OBS recording  
✅ **High Performance**: Lock-free queue, zero input lag  
✅ **Comprehensive**: Captures keyboard, mouse, and gamepad  
✅ **Accurate Timing**: Records delays between events  
✅ **Easy to Use**: No configuration needed  

## Quick Start

### 1. Build the Plugin

```bash
cd input-overlay
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### 2. Install the Plugin

Copy the built plugin to your OBS plugins directory:
- **Windows**: `C:\Program Files\obs-studio\obs-plugins\64bit\`
- **Linux**: `~/.config/obs-studio/plugins/`
- **macOS**: `~/Library/Application Support/obs-studio/plugins/`

### 3. Use the Recorder

1. **Start OBS** and load your scene
2. **Click "Start Recording"** in OBS
   - Event recording starts automatically
   - A `.ior` file is created alongside your video
3. **Perform your actions** (gaming, tutorial, etc.)
4. **Click "Stop Recording"** in OBS
   - Event recording stops automatically
   - All events are saved to the `.ior` file

### 4. View Recorded Events

Build and run the example reader:

```bash
cd examples
g++ -std=c++17 event_reader_example.cpp -o event_reader

# View all events
./event_reader recording.ior

# View statistics only
./event_reader recording.ior --stats-only
```

## Output Example

```
   Index |    Timestamp | Event Type                | Details
--------------------------------------------------------------------------------
       0 |  1234567890 ns | KEYBOARD_PRESS            | keycode=65
       1 |  1234568000 ns | SLEEP                     | duration=110 ns
       2 |  1234568100 ns | KEYBOARD_RELEASE          | keycode=65
       3 |  1234569000 ns | MOUSE_MOVE                | x=500 y=300
       4 |  1234570000 ns | MOUSE_PRESS               | button=1 x=500 y=300

=== Recording Statistics ===
Total events: 15234
  Keyboard events: 5432
  Mouse events: 8901
  Gamepad events: 901
  Sleep events: 0

Recording duration: 120.5 seconds
File size: 365616 bytes (357.05 KB)
```

## File Format

- **Extension**: `.ior` (Input Overlay Recording)
- **Format**: Binary (fixed-size records)
- **Location**: Same directory as your video file
- **Naming**: Matches your video filename
  - Video: `gameplay.mp4`
  - Recording: `gameplay.ior`

## Reading Events in Your Code

```cpp
#include <fstream>
#include "recorder/event_recorder.hpp"

std::ifstream file("recording.ior", std::ios::binary);
recorder::RecordedEvent event;

while (file.read(reinterpret_cast<char*>(&event), sizeof(event))) {
    switch (event.type) {
        case recorder::EventType::KEYBOARD_PRESS:
            printf("Key pressed: %d\n", event.data.keyboard.keycode);
            break;
        case recorder::EventType::MOUSE_MOVE:
            printf("Mouse moved to: %d, %d\n", 
                   event.data.mouse.x, event.data.mouse.y);
            break;
        case recorder::EventType::SLEEP:
            printf("Sleep for: %llu ns\n", 
                   event.data.sleep.duration_ns);
            break;
        // ... handle other event types
    }
}
```

## Event Types

| Event Type | Description |
|------------|-------------|
| `KEYBOARD_PRESS` | Key pressed |
| `KEYBOARD_RELEASE` | Key released |
| `MOUSE_PRESS` | Mouse button pressed |
| `MOUSE_RELEASE` | Mouse button released |
| `MOUSE_MOVE` | Mouse moved |
| `MOUSE_WHEEL` | Mouse wheel scrolled |
| `GAMEPAD_BUTTON_PRESS` | Gamepad button pressed |
| `GAMEPAD_BUTTON_RELEASE` | Gamepad button released |
| `GAMEPAD_AXIS` | Gamepad axis moved |
| `SLEEP` | Time delay between events |

## Performance

- **Memory**: ~2.4 MB for 100,000 queued events
- **CPU**: < 1% overhead on modern systems
- **Disk**: ~24 bytes per event
- **Example**: 1 hour @ 100 events/sec = ~8.6 MB file

## Troubleshooting

### `.ior` file not created?
- Check OBS recording path permissions
- Verify plugin is loaded in OBS
- Check OBS logs for errors

### File too large?
- Mouse movement events are frequent
- This is normal for active mouse usage
- Future versions will support filtering

### Missing events?
- Check if input filtering is enabled in plugin settings
- Verify uiohook and gamepad hooks are running

## Next Steps

- Read the [full documentation](EVENT_RECORDER_DOCUMENTATION.md)
- Check the [implementation summary](RECORDER_IMPLEMENTATION_SUMMARY.md)
- Explore the [example reader code](examples/event_reader_example.cpp)

## Support

For issues or questions:
1. Check the documentation files
2. Review OBS logs
3. Open an issue on GitHub

## License

GNU General Public License v2.0 (same as input-overlay)
