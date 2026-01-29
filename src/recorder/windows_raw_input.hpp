/*************************************************************************
 * This file is part of input-overlay
 * git.vrsal.cc/alex/input-overlay
 * Copyright 2025 univrsal <uni@vrsal.xyz>.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *************************************************************************/

#pragma once

#ifdef _WIN32

#include <Windows.h>
#include <cstdint>
#include <functional>
#include <thread>
#include <atomic>

namespace recorder {
namespace raw_input {

/**
 * @brief Windows Raw Input API implementation for high-precision input capture
 * 
 * This implementation provides:
 * - Direct hardware timestamps (QueryPerformanceCounter)
 * - 1000 Hz polling support for gaming peripherals
 * - Sub-millisecond precision (microsecond level)
 * - No Windows message queue delays
 * - Direct kernel driver access
 * 
 * Advantages over Low-Level Hooks (uiohook):
 * - Lower latency (~0.5-1ms vs 1-2ms)
 * - More accurate timestamps (hardware QPC vs GetMessageTime)
 * - Better support for high-frequency devices (1000 Hz mice)
 * - No event batching or throttling
 */

// Callback types for input events
using KeyboardCallback = std::function<void(uint16_t vkey, uint16_t scancode, bool pressed, uint64_t timestamp_ns)>;
using MouseButtonCallback = std::function<void(uint16_t button, int16_t x, int16_t y, bool pressed, uint64_t timestamp_ns)>;
using MouseMoveCallback = std::function<void(int16_t x, int16_t y, int16_t dx, int16_t dy, uint64_t timestamp_ns)>;
using MouseWheelCallback = std::function<void(int16_t delta, uint64_t timestamp_ns)>;

/**
 * @brief Windows Raw Input manager
 */
class RawInputManager {
public:
    RawInputManager();
    ~RawInputManager();

    /**
     * @brief Initialize Raw Input and start capturing
     * @return true if successful
     */
    bool start();

    /**
     * @brief Stop capturing and cleanup
     */
    void stop();

    /**
     * @brief Check if currently capturing
     */
    bool is_running() const { return running.load(std::memory_order_acquire); }

    /**
     * @brief Set callback for keyboard events
     */
    void set_keyboard_callback(KeyboardCallback callback) { keyboard_cb = callback; }

    /**
     * @brief Set callback for mouse button events
     */
    void set_mouse_button_callback(MouseButtonCallback callback) { mouse_button_cb = callback; }

    /**
     * @brief Set callback for mouse move events
     */
    void set_mouse_move_callback(MouseMoveCallback callback) { mouse_move_cb = callback; }

    /**
     * @brief Set callback for mouse wheel events
     */
    void set_mouse_wheel_callback(MouseWheelCallback callback) { mouse_wheel_cb = callback; }

private:
    // Window procedure for receiving Raw Input messages
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    // Process Raw Input data
    void process_raw_input(HRAWINPUT hRawInput);

    // Message loop thread
    void message_loop_thread();

    // Get high-precision timestamp in nanoseconds
    uint64_t get_timestamp_ns() const;

    // Callbacks
    KeyboardCallback keyboard_cb;
    MouseButtonCallback mouse_button_cb;
    MouseMoveCallback mouse_move_cb;
    MouseWheelCallback mouse_wheel_cb;

    // Window handle for receiving messages
    HWND hwnd;
    ATOM window_class;

    // Thread management
    std::thread message_thread;
    std::atomic<bool> running;
    std::atomic<bool> should_stop;

    // Performance counter frequency for timestamp conversion
    LARGE_INTEGER qpc_frequency;

    // Mouse position tracking (for absolute coordinates)
    POINT last_mouse_pos;

    // Statistics
    std::atomic<uint64_t> keyboard_event_count;
    std::atomic<uint64_t> mouse_event_count;
};

/**
 * @brief Initialize the Raw Input system
 * @return true if successful
 */
bool init();

/**
 * @brief Cleanup the Raw Input system
 */
void cleanup();

/**
 * @brief Start capturing input
 * @return true if successful
 */
bool start_capture();

/**
 * @brief Stop capturing input
 */
void stop_capture();

/**
 * @brief Check if currently capturing
 */
bool is_capturing();

/**
 * @brief Set keyboard event callback
 */
void set_keyboard_callback(KeyboardCallback callback);

/**
 * @brief Set mouse button event callback
 */
void set_mouse_button_callback(MouseButtonCallback callback);

/**
 * @brief Set mouse move event callback
 */
void set_mouse_move_callback(MouseMoveCallback callback);

/**
 * @brief Set mouse wheel event callback
 */
void set_mouse_wheel_callback(MouseWheelCallback callback);

} // namespace raw_input
} // namespace recorder

#endif // _WIN32
