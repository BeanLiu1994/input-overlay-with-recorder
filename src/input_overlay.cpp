/*************************************************************************
 * This file is part of input-overlay
 * git.vrsal.xyz/alex/input-overlay
 * Copyright 2023 univrsal <uni@vrsal.xyz>.
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

#include <QTimer>
#include <QAction>
#include <QMainWindow>
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs-output.h>
#include <util/config-file.h>
#include <thread>

#include "gui/io_settings_dialog.hpp"
#include "hook/gamepad_hook_helper.hpp"
#include "hook/uiohook_helper.hpp"
#include "network/remote_connection.hpp"
#include "network/websocket_server.hpp"
#include "sources/input_source.hpp"
#include "util/config.hpp"
#include "util/log.h"
#include "util/lang.h"
#include "recorder/event_recorder.hpp"
#include "plugin-macros.generated.h"

#ifdef LINUX

extern void cleanupDisplay();

#endif

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("input-overlay", "en-US")

// OBS frontend event callback for recording start/stop
static void frontend_event_callback(enum obs_frontend_event event, void *private_data)
{
    UNUSED_PARAMETER(private_data);
    
    switch (event) {
        case OBS_FRONTEND_EVENT_RECORDING_STARTED:
            binfo("Recording started - starting event recorder");
            if (recorder::g_recorder) {
                std::string output_path;
                
                // Get the recording output object
                obs_output_t* recording_output = obs_frontend_get_recording_output();
                if (recording_output) {
                    // Get the output settings which contains the file path
                    obs_data_t* settings = obs_output_get_settings(recording_output);
                    if (settings) {
                        const char* file_path = obs_data_get_string(settings, "path");
                        if (file_path && file_path[0] != '\0') {
                            output_path = file_path;
                            binfo("[EventRecorder] OBS recording file: %s", file_path);
                            
                            // Replace video extension with .ior (input overlay recording)
                            size_t ext_pos = output_path.find_last_of('.');
                            if (ext_pos != std::string::npos) {
                                output_path = output_path.substr(0, ext_pos) + ".ior";
                            } else {
                                output_path += ".ior";
                            }
                            
                            binfo("[EventRecorder] Event recording will be saved to: %s", output_path.c_str());
                        }
                        obs_data_release(settings);
                    }
                    obs_output_release(recording_output);
                }
                
                // Fallback: if we couldn't get the path, use timestamp-based filename
                if (output_path.empty()) {
                    auto now = std::chrono::system_clock::now();
                    auto time_t = std::chrono::system_clock::to_time_t(now);
                    char timestamp[64];
                    std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", std::localtime(&time_t));
                    output_path = std::string("input_recording_") + timestamp + ".ior";
                    
                    bwarn("[EventRecorder] Could not get OBS recording path, using fallback: %s", output_path.c_str());
                }
                
                recorder::start_recording(output_path);
            }
            break;
            
        case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
            binfo("Recording stopped - stopping event recorder");
            if (recorder::g_recorder) {
                recorder::stop_recording();
            }
            break;
            
        default:
            break;
    }
}

bool obs_module_load()
{
    binfo("Loading v%s-%s (%s) build time %s", PLUGIN_VERSION, GIT_BRANCH, GIT_COMMIT_HASH, BUILD_TIME);
    io_config::set_defaults();
    io_config::load();

    // Initialize event recorder
    recorder::init();
    
    // Register OBS frontend event callback for recording start/stop
    obs_frontend_add_event_callback(frontend_event_callback, nullptr);

    if (io_config::enable_overlay_source)
        sources::register_overlay_source();

    std::thread tmp([] {
        if (io_config::enable_gamepad_hook)
            gamepad_hook::start();
        if (io_config::enable_websocket_server)
            wss::start();
        network::local_input = io_config::enable_gamepad_hook || io_config::enable_uiohook;
        /* Input filtering via focused window title */
        if (io_config::enable_input_control)
            io_config::io_window_filters.read_from_config();

        if (io_config::enable_uiohook)
            uiohook::start();
    });
    tmp.detach(); // Don't unnecessarily slow down startup

    // UI registration also takes a bit so we delay it, but I guess this is technically cheating
    QTimer::singleShot(0.5, static_cast<QMainWindow *>(obs_frontend_get_main_window()), [] {
        const auto menu_action = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(T_MENU_OPEN_SETTINGS));
        const auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
        obs_frontend_push_ui_translation(obs_module_get_string);
        settings_dialog = new io_settings_dialog(main_window);
        obs_frontend_pop_ui_translation();

        const auto menu_cb = [] { settings_dialog->toggleShowHide(); };
        QAction::connect(menu_action, &QAction::triggered, menu_cb);
    });
    return true;
}

void obs_module_unload()
{
    // Remove OBS frontend event callback
    obs_frontend_remove_event_callback(frontend_event_callback, nullptr);
    
    // Cleanup event recorder
    recorder::cleanup();
    
    gamepad_hook::stop();
    uiohook::stop();
    wss::stop();

#ifdef LINUX
    cleanupDisplay();
#endif
}
