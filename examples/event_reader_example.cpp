/*************************************************************************
 * Event Recorder Reader Example
 * 
 * This example demonstrates how to read and parse .ior files
 * created by the input-overlay event recorder.
 * 
 * Compile: g++ -std=c++17 event_reader_example.cpp -o event_reader
 * Usage: ./event_reader recording.ior
 *************************************************************************/

#include <iostream>
#include <fstream>
#include <string>
#include <cstdint>
#include <iomanip>

// Event types (must match event_recorder.hpp)
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
    SLEEP
};

// Recorded event structure (must match event_recorder.hpp)
struct RecordedEvent {
    EventType type;
    uint64_t timestamp;
    union {
        struct {
            uint16_t keycode;
        } keyboard;
        struct {
            uint16_t button;
            int16_t x;
            int16_t y;
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
            uint64_t duration_ns;
        } sleep;
    } data;
};

// Convert event type to string
const char* event_type_to_string(EventType type) {
    switch (type) {
        case EventType::KEYBOARD_PRESS: return "KEYBOARD_PRESS";
        case EventType::KEYBOARD_RELEASE: return "KEYBOARD_RELEASE";
        case EventType::MOUSE_PRESS: return "MOUSE_PRESS";
        case EventType::MOUSE_RELEASE: return "MOUSE_RELEASE";
        case EventType::MOUSE_MOVE: return "MOUSE_MOVE";
        case EventType::MOUSE_WHEEL: return "MOUSE_WHEEL";
        case EventType::GAMEPAD_BUTTON_PRESS: return "GAMEPAD_BUTTON_PRESS";
        case EventType::GAMEPAD_BUTTON_RELEASE: return "GAMEPAD_BUTTON_RELEASE";
        case EventType::GAMEPAD_AXIS: return "GAMEPAD_AXIS";
        case EventType::SLEEP: return "SLEEP";
        default: return "UNKNOWN";
    }
}

// Print event details
void print_event(const RecordedEvent& event, size_t index) {
    std::cout << std::setw(8) << index << " | ";
    std::cout << std::setw(12) << event.timestamp << " ns | ";
    std::cout << std::setw(25) << event_type_to_string(event.type) << " | ";
    
    switch (event.type) {
        case EventType::KEYBOARD_PRESS:
        case EventType::KEYBOARD_RELEASE:
            std::cout << "keycode=" << event.data.keyboard.keycode;
            break;
            
        case EventType::MOUSE_PRESS:
        case EventType::MOUSE_RELEASE:
            std::cout << "button=" << event.data.mouse.button 
                      << " x=" << event.data.mouse.x 
                      << " y=" << event.data.mouse.y;
            break;
            
        case EventType::MOUSE_MOVE:
            std::cout << "x=" << event.data.mouse.x 
                      << " y=" << event.data.mouse.y;
            break;
            
        case EventType::MOUSE_WHEEL:
            std::cout << "rotation=" << event.data.wheel.rotation 
                      << " delta=" << event.data.wheel.delta;
            break;
            
        case EventType::GAMEPAD_BUTTON_PRESS:
        case EventType::GAMEPAD_BUTTON_RELEASE:
            std::cout << "gamepad=" << (int)event.data.gamepad_button.gamepad_id 
                      << " button=" << (int)event.data.gamepad_button.button;
            break;
            
        case EventType::GAMEPAD_AXIS:
            std::cout << "gamepad=" << (int)event.data.gamepad_axis.gamepad_id 
                      << " axis=" << (int)event.data.gamepad_axis.axis 
                      << " value=" << event.data.gamepad_axis.value;
            break;
            
        case EventType::SLEEP:
            std::cout << "duration=" << event.data.sleep.duration_ns << " ns ("
                      << (event.data.sleep.duration_ns / 1000000.0) << " ms)";
            break;
            
        default:
            std::cout << "unknown event type";
            break;
    }
    
    std::cout << std::endl;
}

// Statistics structure
struct Statistics {
    size_t total_events = 0;
    size_t keyboard_events = 0;
    size_t mouse_events = 0;
    size_t gamepad_events = 0;
    size_t sleep_events = 0;
    uint64_t total_duration_ns = 0;
    uint64_t first_timestamp = 0;
    uint64_t last_timestamp = 0;
};

// Calculate statistics
void calculate_statistics(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file " << filename << std::endl;
        return;
    }
    
    Statistics stats;
    RecordedEvent event;
    
    while (file.read(reinterpret_cast<char*>(&event), sizeof(RecordedEvent))) {
        stats.total_events++;
        
        if (stats.first_timestamp == 0) {
            stats.first_timestamp = event.timestamp;
        }
        stats.last_timestamp = event.timestamp;
        
        switch (event.type) {
            case EventType::KEYBOARD_PRESS:
            case EventType::KEYBOARD_RELEASE:
                stats.keyboard_events++;
                break;
                
            case EventType::MOUSE_PRESS:
            case EventType::MOUSE_RELEASE:
            case EventType::MOUSE_MOVE:
            case EventType::MOUSE_WHEEL:
                stats.mouse_events++;
                break;
                
            case EventType::GAMEPAD_BUTTON_PRESS:
            case EventType::GAMEPAD_BUTTON_RELEASE:
            case EventType::GAMEPAD_AXIS:
                stats.gamepad_events++;
                break;
                
            case EventType::SLEEP:
                stats.sleep_events++;
                stats.total_duration_ns += event.data.sleep.duration_ns;
                break;
                
            default:
                break;
        }
    }
    
    // Print statistics
    std::cout << "\n=== Recording Statistics ===" << std::endl;
    std::cout << "Total events: " << stats.total_events << std::endl;
    std::cout << "  Keyboard events: " << stats.keyboard_events << std::endl;
    std::cout << "  Mouse events: " << stats.mouse_events << std::endl;
    std::cout << "  Gamepad events: " << stats.gamepad_events << std::endl;
    std::cout << "  Sleep events: " << stats.sleep_events << std::endl;
    
    if (stats.last_timestamp > stats.first_timestamp) {
        uint64_t recording_duration = stats.last_timestamp - stats.first_timestamp;
        std::cout << "\nRecording duration: " 
                  << (recording_duration / 1000000000.0) << " seconds" << std::endl;
    }
    
    if (stats.total_duration_ns > 0) {
        std::cout << "Total sleep time: " 
                  << (stats.total_duration_ns / 1000000000.0) << " seconds" << std::endl;
    }
    
    // Calculate file size
    file.clear();
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    std::cout << "\nFile size: " << file_size << " bytes ("
              << (file_size / 1024.0) << " KB)" << std::endl;
    std::cout << "Average event size: " 
              << (stats.total_events > 0 ? file_size / stats.total_events : 0) 
              << " bytes" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <recording.ior> [--stats-only]" << std::endl;
        std::cerr << "  --stats-only: Only show statistics, don't print all events" << std::endl;
        return 1;
    }
    
    std::string filename = argv[1];
    bool stats_only = (argc > 2 && std::string(argv[2]) == "--stats-only");
    
    if (!stats_only) {
        // Read and print all events
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Error: Cannot open file " << filename << std::endl;
            return 1;
        }
        
        std::cout << "Reading events from: " << filename << std::endl;
        std::cout << "\n" << std::setw(8) << "Index" << " | "
                  << std::setw(12) << "Timestamp" << " | "
                  << std::setw(25) << "Event Type" << " | "
                  << "Details" << std::endl;
        std::cout << std::string(80, '-') << std::endl;
        
        RecordedEvent event;
        size_t index = 0;
        
        while (file.read(reinterpret_cast<char*>(&event), sizeof(RecordedEvent))) {
            print_event(event, index++);
            
            // Limit output for very large files
            if (index >= 1000 && !stats_only) {
                std::cout << "\n... (showing first 1000 events only, use --stats-only for full analysis)" << std::endl;
                break;
            }
        }
        
        file.close();
    }
    
    // Calculate and print statistics
    calculate_statistics(filename);
    
    return 0;
}
