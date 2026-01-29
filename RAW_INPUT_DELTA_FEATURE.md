# Raw Input Delta Feature

> **Mouse movement now includes raw hardware delta values from Windows Raw Input API**

---

## Overview

The event recorder now captures **raw mouse movement deltas** directly from the Windows Raw Input API. This provides **hardware-level movement data** that is independent of Windows pointer acceleration and sensitivity settings.

## What Changed

### Before ❌
```
125.456 MOUSE_MOVE 1920 1080
```
- Only absolute screen coordinates
- No information about actual mouse movement

### After ✅
```
125.456 MOUSE_MOVE 1920 1080 -10 5
```
- Absolute screen coordinates (x, y)
- **Raw hardware delta** (dx, dy) in mickeys

---

## Technical Details

### What are "Mickeys"?

A **mickey** is the smallest unit of mouse movement detected by the hardware:

- **Definition**: 1 mickey = 1/400th of an inch of physical mouse movement
- **Hardware-level**: Reported directly by the mouse sensor
- **DPI-independent**: Same physical movement = same mickey count regardless of DPI
- **Unaffected by software**: Windows sensitivity/acceleration doesn't change mickey values

### Delta Values

The `dx` and `dy` values represent:

- **dx**: Horizontal movement in mickeys (negative = left, positive = right)
- **dy**: Vertical movement in mickeys (negative = up, positive = down)
- **Source**: `RAWMOUSE.lLastX` and `RAWMOUSE.lLastY` from Windows Raw Input API
- **Precision**: 16-bit signed integer (-32768 to +32767)

### Example Interpretation

```
125.456 MOUSE_MOVE 1920 1080 -10 5
```

**What this means:**
1. **Timestamp**: 125.456ms from recording start
2. **Screen Position**: Mouse cursor is at (1920, 1080) pixels
3. **Hardware Movement**: 
   - Moved **left** by 10 mickeys
   - Moved **down** by 5 mickeys
4. **Physical Movement**: ~0.025 inches left, ~0.0125 inches down (at 400 DPI)

---

## Use Cases

### 1. Gaming Analysis 🎮

**Aim Training:**
```python
# Calculate total mouse movement distance
total_dx = sum(abs(event.dx) for event in mouse_moves)
total_dy = sum(abs(event.dy) for event in mouse_moves)
print(f"Total mouse movement: {total_dx} horizontal, {total_dy} vertical mickeys")
```

**Sensitivity Calculation:**
```python
# Determine effective sensitivity
screen_pixels_moved = abs(x2 - x1)
mickeys_moved = sum(abs(dx) for dx in deltas)
sensitivity = screen_pixels_moved / mickeys_moved
print(f"Effective sensitivity: {sensitivity:.2f} pixels/mickey")
```

### 2. Input Replay 🔄

**Reconstruct Exact Movement:**
```python
# Replay mouse movement using raw deltas
for event in events:
    if event.type == 'MOUSE_MOVE':
        # Use dx, dy to reconstruct movement
        simulate_mouse_delta(event.dx, event.dy)
```

### 3. Smoothness Analysis 📊

**Detect Jitter:**
```python
# Calculate movement consistency
deltas = [(event.dx, event.dy) for event in mouse_moves]
variance = calculate_variance(deltas)
print(f"Movement smoothness: {1/variance:.2f}")
```

### 4. DPI Detection 🔍

**Estimate Mouse DPI:**
```python
# Compare physical movement to screen movement
physical_inches = total_mickeys / 400  # 400 mickeys per inch
screen_pixels = abs(x_end - x_start)
dpi = screen_pixels / physical_inches
print(f"Estimated DPI: {dpi:.0f}")
```

---

## File Format Changes

### MOUSE_MOVE Event Format

**Old Format:**
```
timestamp MOUSE_MOVE x y
```

**New Format:**
```
timestamp MOUSE_MOVE x y dx dy
```

### Backward Compatibility

⚠️ **Breaking Change**: Old parsers expecting only `x y` will need to be updated to handle `x y dx dy`.

**Migration:**
```python
# Old parser
parts = line.split()
x, y = int(parts[2]), int(parts[3])

# New parser
parts = line.split()
if len(parts) >= 6:  # New format with deltas
    x, y, dx, dy = int(parts[2]), int(parts[3]), int(parts[4]), int(parts[5])
else:  # Old format (backward compatibility)
    x, y = int(parts[2]), int(parts[3])
    dx, dy = 0, 0  # No delta information
```

---

## Code Changes

### 1. Callback Signature

**windows_raw_input.hpp:**
```cpp
// Old
using MouseMoveCallback = std::function<void(int16_t x, int16_t y, uint64_t timestamp_ns)>;

// New
using MouseMoveCallback = std::function<void(int16_t x, int16_t y, int16_t dx, int16_t dy, uint64_t timestamp_ns)>;
```

### 2. Raw Input Processing

**windows_raw_input.cpp:**
```cpp
// Capture raw delta values
int16_t dx = static_cast<int16_t>(mouse.lLastX);
int16_t dy = static_cast<int16_t>(mouse.lLastY);

// Pass to callback
if (mouse_move_cb) {
    mouse_move_cb(cursor_pos.x, cursor_pos.y, dx, dy, timestamp_ns);
}
```

### 3. Event Structure

**event_recorder.hpp:**
```cpp
struct {
    uint16_t button;
    int16_t x;
    int16_t y;
    int16_t dx;  // Raw delta X from Raw Input
    int16_t dy;  // Raw delta Y from Raw Input
} mouse;
```

### 4. File Output

**event_recorder.cpp:**
```cpp
case EventType::MOUSE_MOVE:
    line += "MOUSE_MOVE " + std::to_string(event.data.mouse.x) + " " + 
            std::to_string(event.data.mouse.y) + " " +
            std::to_string(event.data.mouse.dx) + " " +
            std::to_string(event.data.mouse.dy);
    break;
```

---

## Benefits

### ✅ Hardware-Level Precision

- **Direct from sensor**: No software interference
- **True movement**: Unaffected by acceleration curves
- **Consistent**: Same physical movement = same delta value

### ✅ Gaming Applications

- **Aim analysis**: Measure actual mouse control
- **Sensitivity tuning**: Calculate optimal settings
- **Input replay**: Reconstruct exact movements
- **Cheat detection**: Analyze movement patterns

### ✅ Research & Analysis

- **Motor control studies**: Analyze human movement patterns
- **Accessibility research**: Study input difficulties
- **Performance metrics**: Measure input precision
- **Device comparison**: Compare different mice objectively

---

## Platform Support

| Platform | Delta Support | Notes |
|----------|---------------|-------|
| **Windows** | ✅ Full | Via Raw Input API (`RAWMOUSE.lLastX/Y`) |
| **Linux** | ❌ Not yet | uiohook doesn't provide deltas (outputs 0, 0) |
| **macOS** | ❌ Not yet | uiohook doesn't provide deltas (outputs 0, 0) |

**Note**: On non-Windows platforms, delta values will be `0 0` since uiohook only provides absolute coordinates.

---

## Example Output

### Sample Recording

```
0.000 START 2026-01-29 15:16:37.000
5.642 MOUSE_MOVE 5561 1569 -47 7
13.566 MOUSE_MOVE 5514 1562 -53 -8
21.946 MOUSE_MOVE 5461 1554 -82 -21
37.324 MOUSE_MOVE 5343 1533 -63 -8
45.346 MOUSE_MOVE 5280 1525 -60 -13
53.318 MOUSE_MOVE 5220 1512 -63 -19
61.453 MOUSE_MOVE 5157 1493 -63 -18
69.569 MOUSE_MOVE 5094 1475 -61 -16
77.957 MOUSE_MOVE 5033 1459 -58 -13
```

### Analysis

```python
# Calculate total movement
total_horizontal = sum(abs(dx) for dx in deltas_x)  # 550 mickeys
total_vertical = sum(abs(dy) for dy in deltas_y)    # 123 mickeys

# Physical distance (at 400 DPI)
horizontal_inches = 550 / 400  # 1.375 inches
vertical_inches = 123 / 400    # 0.308 inches

# Screen distance
screen_pixels_x = abs(5033 - 5561)  # 528 pixels
screen_pixels_y = abs(1459 - 1569)  # 110 pixels

# Effective sensitivity
sensitivity_x = 528 / 550  # 0.96 pixels/mickey
sensitivity_y = 110 / 123  # 0.89 pixels/mickey
```

---

## Future Enhancements

### Potential Improvements

1. **Linux Support** - Implement X11/Wayland raw input capture
2. **macOS Support** - Use IOKit for raw HID events
3. **Acceleration Detection** - Compare deltas to screen movement
4. **DPI Auto-detection** - Calculate mouse DPI from movement data
5. **Movement Visualization** - Generate heatmaps from delta data

---

## Summary

The Raw Input delta feature provides:

✅ **Hardware-level mouse movement data**  
✅ **Independent of Windows settings**  
✅ **Perfect for gaming analysis**  
✅ **Enables accurate input replay**  
✅ **Supports research applications**  
✅ **Microsecond-precision timestamps**  

**Perfect for**:
- Gaming aim analysis
- Input replay systems
- Motor control research
- Device comparison
- Sensitivity optimization

---

**Last Updated**: 2026-01-29  
**Version**: 1.1  
**Status**: ✅ Production Ready (Windows only)
