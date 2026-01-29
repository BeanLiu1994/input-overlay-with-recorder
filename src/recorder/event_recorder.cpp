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

#include "event_recorder.hpp"
#include "../util/log.h"
#include <util/platform.h>
#include <fstream>
#include <chrono>

namespace recorder {

std::unique_ptr<EventRecorder> g_recorder = nullptr;

EventRecorder::EventRecorder()
    : is_recording(false)
    , should_stop_writer(false)
    , recording_start_time(0)
    , last_event_time(0)
{
    binfo("[EventRecorder] Constructor called");
}

EventRecorder::~EventRecorder()
{
    binfo("[EventRecorder] Destructor called");
    if (is_recording.load(std::memory_order_acquire)) {
        bwarn("[EventRecorder] Still recording in destructor, stopping...");
        stop_recording();
    }
}

void EventRecorder::start_recording(const std::string& output_path)
{
    if (is_recording.load(std::memory_order_acquire)) {
        bwarn("[EventRecorder] Already recording, stopping previous recording");
        stop_recording();
    }

    binfo("[EventRecorder] ========================================");
    binfo("[EventRecorder] Starting event recording");
    binfo("[EventRecorder] Output file: %s", output_path.c_str());
    
    // Clear the queue
    size_t old_queue_size = event_queue.size();
    event_queue.clear();
    if (old_queue_size > 0) {
        binfo("[EventRecorder] Cleared %zu old events from queue", old_queue_size);
    }
    
    output_file_path = output_path;
    recording_start_time = os_gettime_ns();
    last_event_time = recording_start_time;
    is_recording.store(true, std::memory_order_release);
    should_stop_writer.store(false, std::memory_order_release);
    
    // Start writer thread
    writer_thread = std::thread(&EventRecorder::writer_thread_func, this);
    binfo("[EventRecorder] Writer thread started");
    binfo("[EventRecorder] Recording is now ACTIVE");
    binfo("[EventRecorder] ========================================");
}

void EventRecorder::stop_recording()
{
    if (!is_recording.load(std::memory_order_acquire)) {
        binfo("[EventRecorder] stop_recording called but not recording");
        return;
    }

    binfo("[EventRecorder] ========================================");
    binfo("[EventRecorder] Stopping event recording");
    size_t remaining_events = event_queue.size();
    binfo("[EventRecorder] Queue has %zu events remaining to save", remaining_events);
    
    is_recording.store(false, std::memory_order_release);
    should_stop_writer.store(true, std::memory_order_release);
    
    // Wait for writer thread to finish
    if (writer_thread.joinable()) {
        binfo("[EventRecorder] Waiting for writer thread to finish...");
        writer_thread.join();
        binfo("[EventRecorder] Writer thread joined successfully");
    }
    
    uint64_t recording_duration = os_gettime_ns() - recording_start_time;
    double duration_seconds = recording_duration / 1000000000.0;
    binfo("[EventRecorder] Recording duration: %.2f seconds", duration_seconds);
    binfo("[EventRecorder] Final queue size: %zu events", event_queue.size());
    binfo("[EventRecorder] Recording stopped successfully");
    binfo("[EventRecorder] ========================================");
}

void EventRecorder::writer_thread_func()
{
    binfo("[EventRecorder] Writer thread started");
    
    std::vector<RecordedEvent> batch;
    batch.reserve(1000); // Pre-allocate for performance
    
    const auto flush_interval = std::chrono::milliseconds(500); // Flush every 500ms
    auto last_flush = std::chrono::steady_clock::now();
    size_t total_events_saved = 0;
    size_t flush_count = 0;
    
    while (!should_stop_writer.load(std::memory_order_acquire) || !event_queue.empty()) {
        RecordedEvent event;
        bool has_events = false;
        
        // Dequeue events in batch
        while (event_queue.dequeue(event)) {
            batch.push_back(event);
            has_events = true;
            
            // Flush if batch is large enough
            if (batch.size() >= 1000) {
                break;
            }
        }
        
        auto now = std::chrono::steady_clock::now();
        bool should_flush = (now - last_flush) >= flush_interval;
        
        // Save batch to file if we have events and should flush
        if (has_events && (should_flush || batch.size() >= 1000)) {
            flush_count++;
            size_t batch_size = batch.size();
            binfo("[EventRecorder] Flush #%zu: Saving %zu events to file (queue size: %zu)", 
                  flush_count, batch_size, event_queue.size());
            
            if (save_to_file(batch)) {
                total_events_saved += batch_size;
                batch.clear();
                last_flush = now;
                binfo("[EventRecorder] Successfully saved batch. Total events saved: %zu", total_events_saved);
            } else {
                berr("[EventRecorder] Failed to save events to file");
            }
        }
        
        // Sleep briefly if no events
        if (!has_events) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    // Final flush
    if (!batch.empty()) {
        binfo("[EventRecorder] Final flush: Saving %zu remaining events", batch.size());
        if (save_to_file(batch)) {
            total_events_saved += batch.size();
            binfo("[EventRecorder] Final flush successful");
        }
    }
    
    binfo("[EventRecorder] Writer thread finished. Total events saved: %zu", total_events_saved);
}

bool EventRecorder::save_to_file(const std::vector<RecordedEvent>& events)
{
    if (events.empty()) {
        return true;
    }
    
    std::ofstream file(output_file_path, std::ios::app);
    if (!file.is_open()) {
        berr("[EventRecorder] Failed to open file for writing: %s", output_file_path.c_str());
        return false;
    }
    
    // Write events in ASCII text format
    size_t bytes_written = 0;
    for (const auto& event : events) {
        std::string line;
        
        // Format: timestamp_ns event_type event_data
        line = std::to_string(event.timestamp) + " ";
        
        switch (event.type) {
            case EventType::KEYBOARD_PRESS:
                line += "KEY_PRESS " + std::to_string(event.data.keyboard.keycode);
                break;
            case EventType::KEYBOARD_RELEASE:
                line += "KEY_RELEASE " + std::to_string(event.data.keyboard.keycode);
                break;
            case EventType::MOUSE_PRESS:
                line += "MOUSE_PRESS " + std::to_string(event.data.mouse.button) + " " +
                        std::to_string(event.data.mouse.x) + " " + std::to_string(event.data.mouse.y);
                break;
            case EventType::MOUSE_RELEASE:
                line += "MOUSE_RELEASE " + std::to_string(event.data.mouse.button) + " " +
                        std::to_string(event.data.mouse.x) + " " + std::to_string(event.data.mouse.y);
                break;
            case EventType::MOUSE_MOVE:
                line += "MOUSE_MOVE " + std::to_string(event.data.mouse.x) + " " + 
                        std::to_string(event.data.mouse.y);
                break;
            case EventType::MOUSE_WHEEL:
                line += "MOUSE_WHEEL " + std::to_string(event.data.wheel.rotation) + " " +
                        std::to_string(event.data.wheel.delta);
                break;
            case EventType::GAMEPAD_BUTTON_PRESS:
                line += "GAMEPAD_PRESS " + std::to_string(event.data.gamepad_button.gamepad_id) + " " +
                        std::to_string(event.data.gamepad_button.button);
                break;
            case EventType::GAMEPAD_BUTTON_RELEASE:
                line += "GAMEPAD_RELEASE " + std::to_string(event.data.gamepad_button.gamepad_id) + " " +
                        std::to_string(event.data.gamepad_button.button);
                break;
            case EventType::GAMEPAD_AXIS:
                line += "GAMEPAD_AXIS " + std::to_string(event.data.gamepad_axis.gamepad_id) + " " +
                        std::to_string(event.data.gamepad_axis.axis) + " " +
                        std::to_string(event.data.gamepad_axis.value);
                break;
            case EventType::SLEEP:
                line += "SLEEP " + std::to_string(event.data.sleep.duration_ns);
                break;
            default:
                line += "UNKNOWN";
                break;
        }
        
        line += "\n";
        file.write(line.c_str(), line.length());
        bytes_written += line.length();
    }
    
    file.flush();
    bool success = file.good();
    
    if (success) {
        binfo("[EventRecorder] Wrote %zu bytes (%zu events) to file", bytes_written, events.size());
    } else {
        berr("[EventRecorder] File write failed after writing %zu bytes", bytes_written);
    }
    
    return success;
}

void EventRecorder::record_keyboard_event(uint16_t keycode, bool pressed, uint64_t timestamp)
{
    if (!is_recording.load(std::memory_order_acquire)) {
        return;
    }
    
    // Calculate sleep time since last event
    if (last_event_time > 0 && timestamp > last_event_time) {
        uint64_t sleep_duration = timestamp - last_event_time;
        if (sleep_duration > 1000000) { // Only record sleeps > 1ms
            RecordedEvent sleep_event;
            sleep_event.type = EventType::SLEEP;
            sleep_event.timestamp = last_event_time;
            sleep_event.data.sleep.duration_ns = sleep_duration;
            event_queue.enqueue(sleep_event);
        }
    }
    
    RecordedEvent event;
    event.type = pressed ? EventType::KEYBOARD_PRESS : EventType::KEYBOARD_RELEASE;
    event.timestamp = timestamp;
    event.data.keyboard.keycode = keycode;
    
    event_queue.enqueue(event);
    last_event_time = timestamp;
    
    static size_t keyboard_event_count = 0;
    keyboard_event_count++;
    if (keyboard_event_count % 100 == 0) {
        binfo("[EventRecorder] Recorded %zu keyboard events (queue: %zu)", 
              keyboard_event_count, event_queue.size());
    }
}

void EventRecorder::record_mouse_button_event(uint16_t button, int16_t x, int16_t y, bool pressed, uint64_t timestamp)
{
    if (!is_recording.load(std::memory_order_acquire)) {
        return;
    }
    
    // Calculate sleep time
    if (last_event_time > 0 && timestamp > last_event_time) {
        uint64_t sleep_duration = timestamp - last_event_time;
        if (sleep_duration > 1000000) {
            RecordedEvent sleep_event;
            sleep_event.type = EventType::SLEEP;
            sleep_event.timestamp = last_event_time;
            sleep_event.data.sleep.duration_ns = sleep_duration;
            event_queue.enqueue(sleep_event);
        }
    }
    
    RecordedEvent event;
    event.type = pressed ? EventType::MOUSE_PRESS : EventType::MOUSE_RELEASE;
    event.timestamp = timestamp;
    event.data.mouse.button = button;
    event.data.mouse.x = x;
    event.data.mouse.y = y;
    
    event_queue.enqueue(event);
    last_event_time = timestamp;
    
    static size_t mouse_button_count = 0;
    mouse_button_count++;
    if (mouse_button_count % 50 == 0) {
        binfo("[EventRecorder] Recorded %zu mouse button events (queue: %zu)", 
              mouse_button_count, event_queue.size());
    }
}

void EventRecorder::record_mouse_move_event(int16_t x, int16_t y, uint64_t timestamp)
{
    if (!is_recording.load(std::memory_order_acquire)) {
        return;
    }
    
    RecordedEvent event;
    event.type = EventType::MOUSE_MOVE;
    event.timestamp = timestamp;
    event.data.mouse.button = 0;
    event.data.mouse.x = x;
    event.data.mouse.y = y;
    
    event_queue.enqueue(event);
    last_event_time = timestamp;
}

void EventRecorder::record_mouse_wheel_event(int16_t rotation, int16_t delta, uint64_t timestamp)
{
    if (!is_recording.load(std::memory_order_acquire)) {
        return;
    }
    
    RecordedEvent event;
    event.type = EventType::MOUSE_WHEEL;
    event.timestamp = timestamp;
    event.data.wheel.rotation = rotation;
    event.data.wheel.delta = delta;
    
    event_queue.enqueue(event);
    last_event_time = timestamp;
}

void EventRecorder::record_gamepad_button_event(uint8_t gamepad_id, uint8_t button, bool pressed, uint64_t timestamp)
{
    if (!is_recording.load(std::memory_order_acquire)) {
        return;
    }
    
    RecordedEvent event;
    event.type = pressed ? EventType::GAMEPAD_BUTTON_PRESS : EventType::GAMEPAD_BUTTON_RELEASE;
    event.timestamp = timestamp;
    event.data.gamepad_button.gamepad_id = gamepad_id;
    event.data.gamepad_button.button = button;
    
    event_queue.enqueue(event);
    last_event_time = timestamp;
}

void EventRecorder::record_gamepad_axis_event(uint8_t gamepad_id, uint8_t axis, float value, uint64_t timestamp)
{
    if (!is_recording.load(std::memory_order_acquire)) {
        return;
    }
    
    RecordedEvent event;
    event.type = EventType::GAMEPAD_AXIS;
    event.timestamp = timestamp;
    event.data.gamepad_axis.gamepad_id = gamepad_id;
    event.data.gamepad_axis.axis = axis;
    event.data.gamepad_axis.value = value;
    
    event_queue.enqueue(event);
    last_event_time = timestamp;
}

void EventRecorder::record_uiohook_event(const uiohook_event* event)
{
    if (!event || !is_recording.load(std::memory_order_acquire)) {
        return;
    }
    
    uint64_t timestamp = os_gettime_ns();
    
    switch (event->type) {
        case EVENT_KEY_PRESSED:
            record_keyboard_event(event->data.keyboard.keycode, true, timestamp);
            break;
        case EVENT_KEY_RELEASED:
            record_keyboard_event(event->data.keyboard.keycode, false, timestamp);
            break;
        case EVENT_MOUSE_PRESSED:
            record_mouse_button_event(event->data.mouse.button, event->data.mouse.x, event->data.mouse.y, true, timestamp);
            break;
        case EVENT_MOUSE_RELEASED:
            record_mouse_button_event(event->data.mouse.button, event->data.mouse.x, event->data.mouse.y, false, timestamp);
            break;
        case EVENT_MOUSE_MOVED:
        case EVENT_MOUSE_DRAGGED:
            record_mouse_move_event(event->data.mouse.x, event->data.mouse.y, timestamp);
            break;
        case EVENT_MOUSE_WHEEL:
            record_mouse_wheel_event(event->data.wheel.rotation, event->data.wheel.delta, timestamp);
            break;
        default:
            break;
    }
}

void EventRecorder::record_sdl_gamepad_event(const SDL_Event* event, uint8_t gamepad_id)
{
    if (!event || !is_recording.load(std::memory_order_acquire)) {
        return;
    }
    
    uint64_t timestamp = os_gettime_ns();
    
    switch (event->type) {
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            record_gamepad_button_event(gamepad_id, event->gbutton.button, true, timestamp);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            record_gamepad_button_event(gamepad_id, event->gbutton.button, false, timestamp);
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            record_gamepad_axis_event(gamepad_id, event->gaxis.axis, event->gaxis.value / 32767.0f, timestamp);
            break;
        default:
            break;
    }
}

// Global functions
void init()
{
    if (!g_recorder) {
        g_recorder = std::make_unique<EventRecorder>();
        binfo("[EventRecorder] ========================================");
        binfo("[EventRecorder] Event recorder module initialized");
        binfo("[EventRecorder] Ready to record input events");
        binfo("[EventRecorder] ========================================");
    } else {
        binfo("[EventRecorder] Already initialized");
    }
}

void cleanup()
{
    if (g_recorder) {
        binfo("[EventRecorder] ========================================");
        binfo("[EventRecorder] Cleaning up event recorder");
        if (g_recorder->recording()) {
            binfo("[EventRecorder] Recording still active, stopping...");
            g_recorder->stop_recording();
        }
        g_recorder.reset();
        binfo("[EventRecorder] Event recorder cleaned up successfully");
        binfo("[EventRecorder] ========================================");
    }
}

void start_recording(const std::string& output_path)
{
    if (g_recorder) {
        g_recorder->start_recording(output_path);
    } else {
        berr("[EventRecorder] Cannot start recording: recorder not initialized!");
    }
}

void stop_recording()
{
    if (g_recorder) {
        g_recorder->stop_recording();
    } else {
        bwarn("[EventRecorder] Cannot stop recording: recorder not initialized!");
    }
}

bool is_recording()
{
    return g_recorder && g_recorder->recording();
}

} // namespace recorder
