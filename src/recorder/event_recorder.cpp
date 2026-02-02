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
#include <obs-module.h>
#include "../util/log.h"
#include "../util/config.hpp"
#include <util/platform.h>
#include <fstream>
#include <chrono>
#include <cstring>

namespace recorder {

std::unique_ptr<EventRecorder> g_recorder = nullptr;

EventRecorder::EventRecorder()
    : is_recording(false)
    , is_paused(false)
    , should_stop_writer(false)
    , recording_start_time(0)
    , last_event_time(0)
    , sdl_init_time_offset(0)
{
    // Key states map will be initialized as empty
    // Keys will be added to the map as they are pressed
    
    // Calculate SDL init time offset
    // SDL timestamps are relative to SDL_Init(), we need to convert them to Unix epoch
    uint64_t current_unix_time = os_gettime_ns();
    uint64_t current_sdl_time = SDL_GetTicksNS();
    sdl_init_time_offset = current_unix_time - current_sdl_time;
    
    binfo("[EventRecorder] Initialized with SDL time offset: %llu ns", (unsigned long long)sdl_init_time_offset);
}

EventRecorder::~EventRecorder()
{
    if (is_recording.load(std::memory_order_acquire)) {
        stop_recording();
    }
}

void EventRecorder::start_recording(const std::string& output_path)
{
    if (is_recording.load(std::memory_order_acquire)) {
        bwarn("[EventRecorder] Already recording, stopping previous recording");
        stop_recording();
    }

    binfo("[EventRecorder] Starting recording to: %s", output_path.c_str());
    
    // Clear the queue
    event_queue.clear();
    
    // Clear all key states (assume all keys are released at start)
    {
        std::lock_guard<std::mutex> lock(key_states_mutex);
        key_states.clear();
    }
    
    output_file_path = output_path;
    recording_start_time = os_gettime_ns();
    
    // Get Unix epoch time in milliseconds for the START line
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    recording_start_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    
    last_event_time = recording_start_time;
    
    is_recording.store(true, std::memory_order_release);
    is_paused.store(false, std::memory_order_release);
    should_stop_writer.store(false, std::memory_order_release);
    
    // Start writer thread
    writer_thread = std::thread(&EventRecorder::writer_thread_func, this);
}

void EventRecorder::stop_recording()
{
    if (!is_recording.load(std::memory_order_acquire)) {
        binfo("[EventRecorder] stop_recording called but not recording");
        return;
    }

    binfo("[EventRecorder] Stopping recording (%zu events remaining)", event_queue.size());
    
    is_recording.store(false, std::memory_order_release);
    should_stop_writer.store(true, std::memory_order_release);
    
    // Wait for writer thread to finish
    if (writer_thread.joinable()) {
        writer_thread.join();
    }
    
    uint64_t recording_duration = os_gettime_ns() - recording_start_time;
    double duration_seconds = recording_duration / 1000000000.0;
    binfo("[EventRecorder] Recording stopped (%.2f seconds)", duration_seconds);
}

void EventRecorder::pause_recording()
{
    if (!is_recording.load(std::memory_order_acquire)) {
        bwarn("[EventRecorder] Cannot pause: not recording");
        return;
    }
    
    if (is_paused.load(std::memory_order_acquire)) {
        bwarn("[EventRecorder] Already paused");
        return;
    }
    
    // Record PAUSE event
    RecordedEvent event;
    event.type = EventType::PAUSE;
    event.timestamp = os_gettime_ns();
    event_queue.enqueue(event);
    
    is_paused.store(true, std::memory_order_release);
    
    binfo("[EventRecorder] Recording paused");
}

void EventRecorder::resume_recording()
{
    if (!is_recording.load(std::memory_order_acquire)) {
        bwarn("[EventRecorder] Cannot resume: not recording");
        return;
    }
    
    if (!is_paused.load(std::memory_order_acquire)) {
        bwarn("[EventRecorder] Not paused");
        return;
    }
    
    // Record RESUME event
    RecordedEvent event;
    event.type = EventType::RESUME;
    event.timestamp = os_gettime_ns();
    event_queue.enqueue(event);
    
    is_paused.store(false, std::memory_order_release);
    
    binfo("[EventRecorder] Recording resumed");
}

void EventRecorder::writer_thread_func()
{
    // First, create the file and write the START line
    std::ofstream file(output_file_path, std::ios::trunc); // Create/truncate file
    if (file.is_open()) {
        // Use the start time that was captured in start_recording()
        // Format: 0 START <start_timestamp_ms>
        // The first column is always 0 (relative time at start)
        // The third column is the absolute Unix epoch timestamp when recording started (in milliseconds)
        char start_line[128];
        snprintf(start_line, sizeof(start_line), "0 START %llu\n", (unsigned long long)recording_start_time_ms);
        
        file.write(start_line, strlen(start_line));
        file.close();
    } else {
        berr("[EventRecorder] Failed to create output file: %s", output_file_path.c_str());
        // Continue anyway - save_to_file will try to create it
    }
    
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
            
            if (save_to_file(batch)) {
                total_events_saved += batch_size;
                batch.clear();
                last_flush = now;
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
        if (save_to_file(batch)) {
            total_events_saved += batch.size();
        }
    }
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
    
    size_t bytes_written = 0;
    
    for (const auto& event : events) {
        std::string line;
        
        // Calculate relative timestamp in milliseconds from recording start
        // DO NOT adjust for pause - the replay system will handle PAUSE/RESUME events
        uint64_t relative_time_ms = (event.timestamp - recording_start_time) / 1000000ULL;
        
        // Format: timestamp_ms event_type event_data
        // Output format: milliseconds as integer (e.g., "2366" = 2.366 seconds)
        line = std::to_string(relative_time_ms) + " ";
        
        switch (event.type) {
            case EventType::KEYBOARD_PRESS:
                line += "KEY_DOWN " + std::to_string(event.data.keyboard.keycode);
                break;
            case EventType::KEYBOARD_RELEASE:
                line += "KEY_UP " + std::to_string(event.data.keyboard.keycode);
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
                        std::to_string(event.data.mouse.y) + " " +
                        std::to_string(event.data.mouse.dx) + " " +
                        std::to_string(event.data.mouse.dy);
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
            case EventType::PAUSE:
                line += "PAUSE";
                break;
            case EventType::RESUME:
                line += "RESUME";
                break;
            case EventType::SLEEP:
                // Skip SLEEP events - timing is handled by timestamps
                continue;
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
    
    if (!success) {
        berr("[EventRecorder] File write failed");
    }
    
    return success;
}

void EventRecorder::record_keyboard_event(uint16_t keycode, bool pressed, uint64_t timestamp)
{
    if (!is_recording.load(std::memory_order_acquire)) {
        return;
    }
    
    // Check if keyboard recording is enabled
    if (!io_config::recorder_enable_keyboard) {
        return;
    }
    
    // Filter out auto-repeat events (duplicate KEY_DOWN without KEY_UP)
    {
        std::lock_guard<std::mutex> lock(key_states_mutex);
        
        // Check if key is already pressed
        auto it = key_states.find(keycode);
        bool was_pressed = (it != key_states.end() && it->second);
        
        if (pressed && was_pressed) {
            // Key is already pressed - this is an auto-repeat event, ignore it
            return;
        }
        
        // Update key state
        key_states[keycode] = pressed;
    }
    
    RecordedEvent event;
    event.type = pressed ? EventType::KEYBOARD_PRESS : EventType::KEYBOARD_RELEASE;
    event.timestamp = timestamp;
    event.data.keyboard.keycode = keycode;
    
    event_queue.enqueue(event);
    last_event_time = timestamp;
}

void EventRecorder::record_mouse_button_event(uint16_t button, int16_t x, int16_t y, bool pressed, uint64_t timestamp)
{
    if (!is_recording.load(std::memory_order_acquire)) {
        return;
    }
    
    // Check if mouse recording is enabled
    if (!io_config::recorder_enable_mouse) {
        return;
    }
    
    RecordedEvent event;
    event.type = pressed ? EventType::MOUSE_PRESS : EventType::MOUSE_RELEASE;
    event.timestamp = timestamp;
    event.data.mouse.button = button;
    event.data.mouse.x = x;
    event.data.mouse.y = y;
    
    event_queue.enqueue(event);
    last_event_time = timestamp;
}

void EventRecorder::record_mouse_move_event(int16_t x, int16_t y, int16_t dx, int16_t dy, uint64_t timestamp)
{
    if (!is_recording.load(std::memory_order_acquire)) {
        return;
    }
    
    // Check if mouse recording is enabled
    if (!io_config::recorder_enable_mouse) {
        return;
    }
    
    RecordedEvent event;
    event.type = EventType::MOUSE_MOVE;
    event.timestamp = timestamp;
    event.data.mouse.button = 0;
    event.data.mouse.x = x;
    event.data.mouse.y = y;
    event.data.mouse.dx = dx;
    event.data.mouse.dy = dy;
    
    event_queue.enqueue(event);
    last_event_time = timestamp;
}

void EventRecorder::record_mouse_wheel_event(int16_t rotation, int16_t delta, uint64_t timestamp)
{
    if (!is_recording.load(std::memory_order_acquire)) {
        return;
    }
    
    // Check if mouse recording is enabled
    if (!io_config::recorder_enable_mouse) {
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
    
    // Check if gamepad recording is enabled
    if (!io_config::recorder_enable_gamepad) {
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
    
    // Check if gamepad recording is enabled
    if (!io_config::recorder_enable_gamepad) {
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
    
    // Use the original event timestamp from uiohook (in milliseconds)
    // Convert to nanoseconds for consistency with other timestamps
    uint64_t timestamp = event->time * 1000000ULL;
    
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
            record_mouse_move_event(event->data.mouse.x, event->data.mouse.y, 0, 0, timestamp);
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
    
    // Convert SDL timestamp (relative to SDL_Init) to Unix epoch time
    // SDL timestamps are in nanoseconds since SDL_Init(), we add the offset to get Unix epoch time
    uint64_t timestamp = 0;
    
    switch (event->type) {
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            timestamp = event->gbutton.timestamp + sdl_init_time_offset;
            record_gamepad_button_event(gamepad_id, event->gbutton.button, true, timestamp);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            timestamp = event->gbutton.timestamp + sdl_init_time_offset;
            record_gamepad_button_event(gamepad_id, event->gbutton.button, false, timestamp);
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            timestamp = event->gaxis.timestamp + sdl_init_time_offset;
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
        binfo("[EventRecorder] Module initialized");
    }
}

void cleanup()
{
    if (g_recorder) {
        if (g_recorder->recording()) {
            g_recorder->stop_recording();
        }
        g_recorder.reset();
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

void pause_recording()
{
    if (g_recorder) {
        g_recorder->pause_recording();
    } else {
        bwarn("[EventRecorder] Cannot pause recording: recorder not initialized!");
    }
}

void resume_recording()
{
    if (g_recorder) {
        g_recorder->resume_recording();
    } else {
        bwarn("[EventRecorder] Cannot resume recording: recorder not initialized!");
    }
}

bool is_recording()
{
    return g_recorder && g_recorder->recording();
}

bool is_paused()
{
    return g_recorder && g_recorder->paused();
}

} // namespace recorder
