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

#ifdef _WIN32

#include "windows_raw_input.hpp"
#include "../util/log.h"
#include <util/platform.h>
#include <hidusage.h>

namespace recorder {
namespace raw_input {

// Global instance
static std::unique_ptr<RawInputManager> g_manager = nullptr;

RawInputManager::RawInputManager()
    : hwnd(nullptr)
    , window_class(0)
    , running(false)
    , should_stop(false)
    , keyboard_event_count(0)
    , mouse_event_count(0)
{
    // Get performance counter frequency for high-precision timestamps
    QueryPerformanceFrequency(&qpc_frequency);
    
    // Initialize mouse position
    GetCursorPos(&last_mouse_pos);
    
    binfo("[RawInput] Initialized with QPC frequency: %lld Hz", qpc_frequency.QuadPart);
}

RawInputManager::~RawInputManager()
{
    stop();
}

uint64_t RawInputManager::get_timestamp_ns() const
{
    // Use os_gettime_ns() to ensure consistent time base with recording_start_time
    // This uses the same time source (Unix epoch) as the rest of the system
    return os_gettime_ns();
}

LRESULT CALLBACK RawInputManager::window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    // Get the RawInputManager instance from window user data
    RawInputManager* manager = reinterpret_cast<RawInputManager*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    
    switch (msg) {
        case WM_INPUT: {
            if (manager) {
                manager->process_raw_input(reinterpret_cast<HRAWINPUT>(lparam));
            }
            return DefWindowProc(hwnd, msg, wparam, lparam);
        }
        
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
            
        default:
            return DefWindowProc(hwnd, msg, wparam, lparam);
    }
}

void RawInputManager::process_raw_input(HRAWINPUT hRawInput)
{
    // Get the size of the Raw Input data
    UINT size = 0;
    GetRawInputData(hRawInput, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
    
    if (size == 0) {
        return;
    }
    
    // Allocate buffer and get the data
    std::vector<BYTE> buffer(size);
    RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(buffer.data());
    
    if (GetRawInputData(hRawInput, RID_INPUT, raw, &size, sizeof(RAWINPUTHEADER)) != size) {
        berr("[RawInput] GetRawInputData failed");
        return;
    }
    
    // Get high-precision timestamp immediately
    uint64_t timestamp_ns = get_timestamp_ns();
    
    // Process based on device type
    if (raw->header.dwType == RIM_TYPEKEYBOARD) {
        // Keyboard event
        RAWKEYBOARD& kb = raw->data.keyboard;
        
        // Extract key information
        uint16_t vkey = kb.VKey;
        uint16_t scancode = kb.MakeCode;
        bool pressed = !(kb.Flags & RI_KEY_BREAK);
        
        // Filter out invalid keys
        if (vkey < 255) {
            keyboard_event_count.fetch_add(1, std::memory_order_relaxed);
            
            if (keyboard_cb) {
                keyboard_cb(vkey, scancode, pressed, timestamp_ns);
            }
            
            // Log first few events for debugging
            static uint64_t log_count = 0;
            if (log_count++ < 10) {
                binfo("[RawInput] Keyboard: vkey=%u, scancode=%u, pressed=%d, ts=%llu ns", 
                      vkey, scancode, pressed, timestamp_ns);
            }
        }
    }
    else if (raw->header.dwType == RIM_TYPEMOUSE) {
        // Mouse event
        RAWMOUSE& mouse = raw->data.mouse;
        
        mouse_event_count.fetch_add(1, std::memory_order_relaxed);
        
        // Handle mouse buttons
        if (mouse.usButtonFlags != 0) {
            // Get current cursor position
            POINT cursor_pos;
            GetCursorPos(&cursor_pos);
            
            // Process button events
            if (mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN) {
                if (mouse_button_cb) {
                    mouse_button_cb(1, cursor_pos.x, cursor_pos.y, true, timestamp_ns);
                }
            }
            if (mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP) {
                if (mouse_button_cb) {
                    mouse_button_cb(1, cursor_pos.x, cursor_pos.y, false, timestamp_ns);
                }
            }
            if (mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN) {
                if (mouse_button_cb) {
                    mouse_button_cb(2, cursor_pos.x, cursor_pos.y, true, timestamp_ns);
                }
            }
            if (mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP) {
                if (mouse_button_cb) {
                    mouse_button_cb(2, cursor_pos.x, cursor_pos.y, false, timestamp_ns);
                }
            }
            if (mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN) {
                if (mouse_button_cb) {
                    mouse_button_cb(3, cursor_pos.x, cursor_pos.y, true, timestamp_ns);
                }
            }
            if (mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP) {
                if (mouse_button_cb) {
                    mouse_button_cb(3, cursor_pos.x, cursor_pos.y, false, timestamp_ns);
                }
            }
            if (mouse.usButtonFlags & RI_MOUSE_BUTTON_4_DOWN) {
                if (mouse_button_cb) {
                    mouse_button_cb(4, cursor_pos.x, cursor_pos.y, true, timestamp_ns);
                }
            }
            if (mouse.usButtonFlags & RI_MOUSE_BUTTON_4_UP) {
                if (mouse_button_cb) {
                    mouse_button_cb(4, cursor_pos.x, cursor_pos.y, false, timestamp_ns);
                }
            }
            if (mouse.usButtonFlags & RI_MOUSE_BUTTON_5_DOWN) {
                if (mouse_button_cb) {
                    mouse_button_cb(5, cursor_pos.x, cursor_pos.y, true, timestamp_ns);
                }
            }
            if (mouse.usButtonFlags & RI_MOUSE_BUTTON_5_UP) {
                if (mouse_button_cb) {
                    mouse_button_cb(5, cursor_pos.x, cursor_pos.y, false, timestamp_ns);
                }
            }
            
            // Handle mouse wheel
            if (mouse.usButtonFlags & RI_MOUSE_WHEEL) {
                short delta = static_cast<short>(mouse.usButtonData);
                if (mouse_wheel_cb) {
                    mouse_wheel_cb(delta, timestamp_ns);
                }
            }
        }
        
        // Handle mouse movement
        if (mouse.lLastX != 0 || mouse.lLastY != 0) {
            // Get absolute cursor position
            POINT cursor_pos;
            GetCursorPos(&cursor_pos);
            
            // Get raw delta values from Raw Input
            int16_t dx = static_cast<int16_t>(mouse.lLastX);
            int16_t dy = static_cast<int16_t>(mouse.lLastY);
            
            // Only report if position actually changed
            if (cursor_pos.x != last_mouse_pos.x || cursor_pos.y != last_mouse_pos.y) {
                if (mouse_move_cb) {
                    mouse_move_cb(cursor_pos.x, cursor_pos.y, dx, dy, timestamp_ns);
                }
                last_mouse_pos = cursor_pos;
            }
        }
    }
}

void RawInputManager::message_loop_thread()
{
    binfo("[RawInput] Message loop thread started");
    
    // Create window class
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"RawInputRecorderWindow";
    
    window_class = RegisterClassEx(&wc);
    if (!window_class) {
        berr("[RawInput] Failed to register window class: %lu", GetLastError());
        return;
    }
    
    // Create invisible window for receiving messages
    hwnd = CreateWindowEx(
        0,
        L"RawInputRecorderWindow",
        L"Raw Input Recorder",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,  // Message-only window
        nullptr,
        GetModuleHandle(nullptr),
        nullptr
    );
    
    if (!hwnd) {
        berr("[RawInput] Failed to create window: %lu", GetLastError());
        return;
    }
    
    // Store this pointer in window user data
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    
    // Register for Raw Input devices
    RAWINPUTDEVICE devices[2];
    
    // Keyboard
    devices[0].usUsagePage = HID_USAGE_PAGE_GENERIC;
    devices[0].usUsage = HID_USAGE_GENERIC_KEYBOARD;
    devices[0].dwFlags = RIDEV_INPUTSINK;  // Receive input even when not in foreground
    devices[0].hwndTarget = hwnd;
    
    // Mouse
    devices[1].usUsagePage = HID_USAGE_PAGE_GENERIC;
    devices[1].usUsage = HID_USAGE_GENERIC_MOUSE;
    devices[1].dwFlags = RIDEV_INPUTSINK;  // Receive input even when not in foreground
    devices[1].hwndTarget = hwnd;
    
    if (!RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE))) {
        berr("[RawInput] Failed to register Raw Input devices: %lu", GetLastError());
        DestroyWindow(hwnd);
        return;
    }
    
    binfo("[RawInput] Successfully registered Raw Input devices");
    binfo("[RawInput] - Keyboard: HID_USAGE_PAGE_GENERIC / HID_USAGE_GENERIC_KEYBOARD");
    binfo("[RawInput] - Mouse: HID_USAGE_PAGE_GENERIC / HID_USAGE_GENERIC_MOUSE");
    binfo("[RawInput] - Flags: RIDEV_INPUTSINK (background capture enabled)");
    
    running.store(true, std::memory_order_release);
    
    // Message loop
    MSG msg;
    while (!should_stop.load(std::memory_order_acquire)) {
        // Process messages with timeout
        BOOL result = PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE);
        
        if (result) {
            if (msg.message == WM_QUIT) {
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            // Sleep briefly to avoid busy-waiting
            Sleep(1);
        }
    }
    
    // Unregister Raw Input devices
    devices[0].dwFlags = RIDEV_REMOVE;
    devices[0].hwndTarget = nullptr;
    devices[1].dwFlags = RIDEV_REMOVE;
    devices[1].hwndTarget = nullptr;
    RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE));
    
    // Cleanup
    if (hwnd) {
        DestroyWindow(hwnd);
        hwnd = nullptr;
    }
    
    if (window_class) {
        UnregisterClass(L"RawInputRecorderWindow", GetModuleHandle(nullptr));
        window_class = 0;
    }
    
    running.store(false, std::memory_order_release);
    
    binfo("[RawInput] Message loop thread finished");
    binfo("[RawInput] Total keyboard events: %llu", keyboard_event_count.load());
    binfo("[RawInput] Total mouse events: %llu", mouse_event_count.load());
}

bool RawInputManager::start()
{
    if (running.load(std::memory_order_acquire)) {
        bwarn("[RawInput] Already running");
        return true;
    }
    
    binfo("[RawInput] ========================================");
    binfo("[RawInput] Starting Windows Raw Input capture");
    binfo("[RawInput] This provides:");
    binfo("[RawInput] - Direct hardware timestamps (QueryPerformanceCounter)");
    binfo("[RawInput] - 1000 Hz polling support");
    binfo("[RawInput] - Sub-millisecond precision");
    binfo("[RawInput] - No message queue delays");
    binfo("[RawInput] ========================================");
    
    should_stop.store(false, std::memory_order_release);
    keyboard_event_count.store(0, std::memory_order_release);
    mouse_event_count.store(0, std::memory_order_release);
    
    // Start message loop thread
    message_thread = std::thread(&RawInputManager::message_loop_thread, this);
    
    // Wait for initialization
    for (int i = 0; i < 100 && !running.load(std::memory_order_acquire); ++i) {
        Sleep(10);
    }
    
    if (!running.load(std::memory_order_acquire)) {
        berr("[RawInput] Failed to start message loop");
        if (message_thread.joinable()) {
            message_thread.join();
        }
        return false;
    }
    
    binfo("[RawInput] Raw Input capture started successfully");
    return true;
}

void RawInputManager::stop()
{
    if (!running.load(std::memory_order_acquire)) {
        return;
    }
    
    binfo("[RawInput] ========================================");
    binfo("[RawInput] Stopping Raw Input capture");
    
    should_stop.store(true, std::memory_order_release);
    
    // Post quit message to message loop
    if (hwnd) {
        PostMessage(hwnd, WM_QUIT, 0, 0);
    }
    
    // Wait for thread to finish
    if (message_thread.joinable()) {
        message_thread.join();
    }
    
    binfo("[RawInput] Raw Input capture stopped");
    binfo("[RawInput] ========================================");
}

// Global functions
bool init()
{
    if (!g_manager) {
        g_manager = std::make_unique<RawInputManager>();
        binfo("[RawInput] Raw Input module initialized");
        return true;
    }
    return true;
}

void cleanup()
{
    if (g_manager) {
        g_manager->stop();
        g_manager.reset();
        binfo("[RawInput] Raw Input module cleaned up");
    }
}

bool start_capture()
{
    if (g_manager) {
        return g_manager->start();
    }
    berr("[RawInput] Cannot start: not initialized");
    return false;
}

void stop_capture()
{
    if (g_manager) {
        g_manager->stop();
    }
}

bool is_capturing()
{
    return g_manager && g_manager->is_running();
}

void set_keyboard_callback(KeyboardCallback callback)
{
    if (g_manager) {
        g_manager->set_keyboard_callback(callback);
    }
}

void set_mouse_button_callback(MouseButtonCallback callback)
{
    if (g_manager) {
        g_manager->set_mouse_button_callback(callback);
    }
}

void set_mouse_move_callback(MouseMoveCallback callback)
{
    if (g_manager) {
        g_manager->set_mouse_move_callback(callback);
    }
}

void set_mouse_wheel_callback(MouseWheelCallback callback)
{
    if (g_manager) {
        g_manager->set_mouse_wheel_callback(callback);
    }
}

} // namespace raw_input
} // namespace recorder

#endif // _WIN32
