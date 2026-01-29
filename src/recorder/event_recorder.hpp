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

#include <atomic>
#include <thread>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <uiohook.h>
#include <SDL3/SDL.h>

namespace recorder {

// Event types for recording
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
    SLEEP // Time delay between events
};

// Recorded event structure
struct RecordedEvent {
    EventType type;
    uint64_t timestamp; // in nanoseconds
    union {
        struct {
            uint16_t keycode;
        } keyboard;
        struct {
            uint16_t button;
            int16_t x;
            int16_t y;
            int16_t dx;  // Mouse movement delta X
            int16_t dy;  // Mouse movement delta Y
        } mouse;
        struct {
            int16_t rotation;
            int16_t delta;
        } wheel;
        struct {
            uint8_t gamepad_id;
            uint8_t button;
        } gamepad_button;
        struct {
            uint8_t gamepad_id;
            uint8_t axis;
            float value;
        } gamepad_axis;
        struct {
            uint64_t duration_ns; // Sleep duration in nanoseconds
        } sleep;
    } data;
};

// Lock-free queue node
template<typename T>
struct QueueNode {
    T data;
    std::atomic<QueueNode*> next;
    
    QueueNode() : next(nullptr) {}
    explicit QueueNode(const T& value) : data(value), next(nullptr) {}
};

// Lock-free SPSC (Single Producer Single Consumer) queue
template<typename T>
class LockFreeQueue {
private:
    std::atomic<QueueNode<T>*> head;
    std::atomic<QueueNode<T>*> tail;
    std::atomic<size_t> size_counter;

public:
    LockFreeQueue() : size_counter(0) {
        QueueNode<T>* dummy = new QueueNode<T>();
        head.store(dummy, std::memory_order_relaxed);
        tail.store(dummy, std::memory_order_relaxed);
    }

    ~LockFreeQueue() {
        while (QueueNode<T>* node = head.load(std::memory_order_relaxed)) {
            head.store(node->next.load(std::memory_order_relaxed), std::memory_order_relaxed);
            delete node;
        }
    }

    void enqueue(const T& value) {
        QueueNode<T>* node = new QueueNode<T>(value);
        QueueNode<T>* prev_tail = tail.exchange(node, std::memory_order_acq_rel);
        prev_tail->next.store(node, std::memory_order_release);
        size_counter.fetch_add(1, std::memory_order_relaxed);
    }

    bool dequeue(T& result) {
        QueueNode<T>* h = head.load(std::memory_order_acquire);
        QueueNode<T>* next = h->next.load(std::memory_order_acquire);
        
        if (next == nullptr) {
            return false; // Queue is empty
        }
        
        result = next->data;
        head.store(next, std::memory_order_release);
        delete h;
        size_counter.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }

    bool empty() const {
        QueueNode<T>* h = head.load(std::memory_order_acquire);
        QueueNode<T>* next = h->next.load(std::memory_order_acquire);
        return next == nullptr;
    }

    size_t size() const {
        return size_counter.load(std::memory_order_relaxed);
    }

    void clear() {
        T dummy;
        while (dequeue(dummy)) {}
    }
};

// Event Recorder class
class EventRecorder {
private:
    LockFreeQueue<RecordedEvent> event_queue;
    std::atomic<bool> is_recording;
    std::atomic<bool> should_stop_writer;
    std::thread writer_thread;
    std::string output_file_path;
    uint64_t recording_start_time; // Unix epoch time in nanoseconds
    uint64_t recording_start_time_ms; // Unix epoch time in milliseconds (for START line)
    uint64_t last_event_time;
    
    // SDL initialization time offset (Unix epoch time when SDL_Init was called)
    // Used to convert SDL timestamps (relative to SDL_Init) to Unix epoch time
    uint64_t sdl_init_time_offset;
    
    // Key state tracking to filter out auto-repeat events
    // Using unordered_map to support all possible keycodes (including large uiohook keycodes)
    std::unordered_map<uint16_t, bool> key_states;
    std::mutex key_states_mutex;

    // Writer thread function
    void writer_thread_func();
    
    // Save events to file
    bool save_to_file(const std::vector<RecordedEvent>& events);

public:
    EventRecorder();
    ~EventRecorder();

    // Start recording - clears queue and starts writer thread
    void start_recording(const std::string& output_path);
    
    // Stop recording - stops writer thread and saves remaining events
    void stop_recording();
    
    // Check if currently recording
    bool recording() const { return is_recording.load(std::memory_order_acquire); }
    
    // Record events
    void record_keyboard_event(uint16_t keycode, bool pressed, uint64_t timestamp);
    void record_mouse_button_event(uint16_t button, int16_t x, int16_t y, bool pressed, uint64_t timestamp);
    void record_mouse_move_event(int16_t x, int16_t y, int16_t dx, int16_t dy, uint64_t timestamp);
    void record_mouse_wheel_event(int16_t rotation, int16_t delta, uint64_t timestamp);
    void record_gamepad_button_event(uint8_t gamepad_id, uint8_t button, bool pressed, uint64_t timestamp);
    void record_gamepad_axis_event(uint8_t gamepad_id, uint8_t axis, float value, uint64_t timestamp);
    
    // Record from uiohook event
    void record_uiohook_event(const uiohook_event* event);
    
    // Record from SDL gamepad event
    void record_sdl_gamepad_event(const SDL_Event* event, uint8_t gamepad_id);
    
    // Get queue size
    size_t queue_size() const { return event_queue.size(); }
};

// Global recorder instance
extern std::unique_ptr<EventRecorder> g_recorder;

// Initialize/cleanup
void init();
void cleanup();

// Start/stop recording
void start_recording(const std::string& output_path);
void stop_recording();

// Check if recording
bool is_recording();

} // namespace recorder
