# Event Recorder Feature

## Overview

The Event Recorder is a new feature added to input-overlay that automatically records all keyboard, mouse, and gamepad input events during OBS recording sessions. The recorded data is saved to a binary file that can be used for replay, analysis, or automation purposes.

## Key Features

### 1. **Lock-Free Queue Architecture**
- Uses a high-performance lock-free SPSC (Single Producer Single Consumer) queue
- Minimal overhead on input event processing
- Thread-safe event collection without blocking

### 2. **Automatic Recording Control**
- **Starts automatically** when OBS recording starts
- **Stops automatically** when OBS recording stops
- Queue is cleared at the start of each recording session
- All remaining events are saved when recording stops

### 3. **Multi-threaded Design**
- **Producer thread**: Captures input events in real-time
- **Consumer thread**: Writes events to disk asynchronously
- Batch writing with configurable flush intervals (500ms default)
- No impact on input event latency

### 4. **Comprehensive Event Support**
- ✅ Keyboard press/release events
- ✅ Mouse button press/release events
- ✅ Mouse movement events
- ✅ Mouse wheel events
- ✅ Gamepad button press/release events
- ✅ Gamepad axis motion events
- ✅ Sleep/delay events (time between inputs)

### 5. **Smart Sleep Recording**
- Automatically calculates time delays between events
- Only records sleeps > 1ms to reduce file size
- Enables accurate replay timing

## File Format

### Output File
- **Extension**: `.ior` (Input Overlay Recording)
- **Format**: Binary
- **Location**: Same directory as OBS video recording
- **Naming**: Matches video filename (e.g., `video.mp4` → `video.ior`)

### Binary Structure
Each event is stored as a `RecordedEvent` structure (fixed size):

```cpp
struct RecordedEvent {
    EventType type;        // 1 byte: Event type
    uint64_t timestamp;    // 8 bytes: Timestamp in nanoseconds
    union {
        // Event-specific data (varies by type)
        keyboard, mouse, wheel, gamepad_button, gamepad_axis, sleep
    } data;
};
```

### Event Types
```cpp
enum class EventType : uint8_t {
    KEYBOARD_PRESS,
    KEYBOARD_RELEASE,
    MOUSE_PRESS,
    MOUSE_RELEASE,
    MOUSE_MOVE,
    MOUSE_WHEEL,
    GAMEPAD_BUTTON_PRESS,
    GAMEPAD_BUTTON_RELEASE,
    GAMEPAD_AXIS,
    SLEEP  // Time delay between events
};
```

## Architecture

### Component Diagram
```
┌─────────────────────────────────────────────────────────────┐
│                    OBS Recording Events                      │
│         (OBS_FRONTEND_EVENT_RECORDING_STARTED/STOPPED)       │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│                   Event Recorder Manager                     │
│  - start_recording() → Clear queue, start writer thread      │
│  - stop_recording()  → Stop writer, save remaining events    │
└────────────────────────┬────────────────────────────────────┘
                         │
         ┌───────────────┴───────────────┐
         │                               │
         ▼                               ▼
┌──────────────────┐           ┌──────────────────┐
│  Input Listeners │           │  Lock-Free Queue │
│                  │           │                  │
│ - uiohook        │──────────▶│  Producer Side   │
│ - gamepad_hook   │           │                  │
└──────────────────┘           └────────┬─────────┘
                                        │
                                        ▼
                               ┌──────────────────┐
                               │  Lock-Free Queue │
                               │                  │
                               │  Consumer Side   │
                               └────────┬─────────┘
                                        │
                                        ▼
                               ┌──────────────────┐
                               │  Writer Thread   │
                               │                  │
                               │ - Batch events   │
                               │ - Flush to disk  │
                               └────────┬─────────┘
                                        │
                                        ▼
                               ┌──────────────────┐
                               │  Output File     │
                               │  (.ior format)   │
                               └──────────────────┘
```

### Data Flow

1. **Input Event Occurs**
   - User presses a key, moves mouse, or uses gamepad
   - Event captured by uiohook or SDL gamepad hook

2. **Event Recording**
   - If recording is active, event is converted to `RecordedEvent`
   - Sleep event is calculated based on time since last event
   - Event is enqueued to lock-free queue (non-blocking)

3. **Asynchronous Writing**
   - Writer thread continuously dequeues events
   - Events are batched (up to 1000 events or 500ms interval)
   - Batch is written to disk in binary format

4. **Recording Stop**
   - Writer thread is signaled to stop
   - All remaining events in queue are flushed to disk
   - File is closed and finalized

## Integration Points

### 1. **uiohook_helper.hpp**
```cpp
inline void process_event(uiohook_event *event)
{
    // ... existing code ...
    
    // Record event if recording is active
    if (recorder::is_recording())
        recorder::g_recorder->record_uiohook_event(event);
}
```

### 2. **gamepad_hook_helper.cpp**
```cpp
void gamepads::event_loop()
{
    while (state) {
        while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST) == 1) {
            // ... existing code ...
            
            // Record gamepad events if recording is active
            if (recorder::is_recording()) {
                recorder::g_recorder->record_sdl_gamepad_event(&event, gamepad_idx);
            }
            
            // ... existing code ...
        }
    }
}
```

### 3. **input_overlay.cpp**
```cpp
static void frontend_event_callback(enum obs_frontend_event event, void *private_data)
{
    switch (event) {
        case OBS_FRONTEND_EVENT_RECORDING_STARTED:
            // Generate output file path
            recorder::start_recording(output_path);
            break;
            
        case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
            recorder::stop_recording();
            break;
    }
}

bool obs_module_load()
{
    // Initialize event recorder
    recorder::init();
    
    // Register OBS frontend event callback
    obs_frontend_add_event_callback(frontend_event_callback, nullptr);
    
    // ... existing code ...
}

void obs_module_unload()
{
    // Remove OBS frontend event callback
    obs_frontend_remove_event_callback(frontend_event_callback, nullptr);
    
    // Cleanup event recorder
    recorder::cleanup();
    
    // ... existing code ...
}
```

## Performance Characteristics

### Memory Usage
- **Queue overhead**: ~24 bytes per node + sizeof(RecordedEvent)
- **RecordedEvent size**: ~24 bytes per event
- **Typical memory**: ~2.4 MB for 100,000 events in queue

### CPU Impact
- **Lock-free operations**: O(1) enqueue/dequeue
- **No mutex contention**: Zero blocking on producer side
- **Batch writing**: Minimizes system calls

### Disk I/O
- **Batch size**: Up to 1000 events per write
- **Flush interval**: 500ms (configurable)
- **File size**: ~24 bytes per event + sleep events
- **Example**: 1 hour of gaming at 100 events/sec = ~8.6 MB

## Usage Example

### For Users
1. Install the updated input-overlay plugin
2. Start OBS and configure your scene
3. Click "Start Recording" in OBS
   - Event recording starts automatically
   - `.ior` file is created alongside video file
4. Perform your actions (gaming, tutorial, etc.)
5. Click "Stop Recording" in OBS
   - Event recording stops automatically
   - All events are saved to `.ior` file

### For Developers
```cpp
// Reading recorded events
std::ifstream file("recording.ior", std::ios::binary);
recorder::RecordedEvent event;

while (file.read(reinterpret_cast<char*>(&event), sizeof(event))) {
    switch (event.type) {
        case recorder::EventType::KEYBOARD_PRESS:
            printf("Key pressed: %d at %llu ns\n", 
                   event.data.keyboard.keycode, 
                   event.timestamp);
            break;
        case recorder::EventType::SLEEP:
            printf("Sleep for %llu ns\n", 
                   event.data.sleep.duration_ns);
            break;
        // ... handle other event types ...
    }
}
```

## Future Enhancements

### Potential Features
1. **Compression**: Add optional zlib/lz4 compression for smaller files
2. **JSON export**: Convert binary format to human-readable JSON
3. **Replay tool**: Standalone application to replay recorded inputs
4. **Event filtering**: Record only specific event types
5. **Metadata**: Add recording metadata (resolution, game name, etc.)
6. **Synchronization**: Embed video timestamps for perfect sync

### Configuration Options (Future)
```cpp
// Potential config options
recorder_config {
    bool enabled;                    // Enable/disable recorder
    bool record_mouse_movement;      // Filter mouse movement
    bool record_gamepad;             // Filter gamepad events
    uint32_t flush_interval_ms;      // Batch flush interval
    uint32_t min_sleep_duration_ns;  // Minimum sleep to record
    std::string output_directory;    // Custom output location
    bool compress;                   // Enable compression
};
```

## Troubleshooting

### Common Issues

**Q: Recording file is not created**
- Check OBS recording path permissions
- Verify plugin is loaded correctly
- Check OBS logs for error messages

**Q: File size is very large**
- Mouse movement events can be frequent
- Consider filtering mouse movement in future versions
- Compression will help in future updates

**Q: Events seem delayed or missing**
- Check if input filtering is enabled in plugin settings
- Verify uiohook and gamepad hooks are running
- Check system performance (CPU/disk)

## Technical Notes

### Thread Safety
- Lock-free queue ensures thread safety without mutexes
- Atomic operations for recording state
- No data races or deadlocks possible

### Platform Support
- ✅ Windows (tested)
- ✅ Linux (should work)
- ✅ macOS (should work)

### Dependencies
- OBS Studio (obs-frontend-api)
- libuiohook (keyboard/mouse)
- SDL3 (gamepad)
- C++17 or later

## License
Same as input-overlay: GNU General Public License v2.0

## Credits
- Based on input-overlay by univrsal
- Lock-free queue implementation inspired by Michael-Scott queue
- Event recording feature added for input-recorder project integration
