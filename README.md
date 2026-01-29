![logo](./docs/io.png)

[![Push to master](https://github.com/univrsal/input-overlay/actions/workflows/push.yaml/badge.svg)](https://github.com/univrsal/input-overlay/actions/workflows/push.yaml)

Show keyboard, mouse and gamepad input on stream.\
Available for OBS Studio on Windows and Linux (64bit).
Head over to [releases](https://github.com/univrsal/input-overlay/releases) for binaries.

## ✨ New Feature: Event Recorder

**Automatically record all input events during OBS recording!**

The Event Recorder captures keyboard, mouse, and gamepad inputs to a `.ior` file alongside your video recording. Perfect for:
- 🎮 Replay analysis and speedrun verification
- 📊 Input pattern analysis and statistics
- 🤖 Automation and macro creation
- 🎓 Tutorial creation and demonstration

**Quick Start:**
1. Configure in input-overlay settings (mouse recording is **BETA** - disabled by default)
2. Start OBS recording → Event recording starts automatically
3. Perform your actions (gaming, tutorial, etc.)
4. Stop OBS recording → Events saved to `.ior` file

📖 **Documentation:**
- [Quick Start Guide](RECORDER_QUICK_START.md)
- [Full Documentation](EVENT_RECORDER_DOCUMENTATION.md)
- [Implementation Details](RECORDER_IMPLEMENTATION_SUMMARY.md)

## [Wiki](https://github.com/univrsal/input-overlay/wiki)
## [Installation](https://github.com/univrsal/input-overlay/wiki/Installation)
## Credits
input-overlay depends on [libuiohook](https://github.com/kwhat/libuiohook) by [kwhat](https://github.com/kwhat) licensed under the [GNU General Public License v3.0](https://www.gnu.org/licenses/gpl-3.0.txt), [mongoose](https://github.com/cesanta/mongoose) licensed under the [GNU General Public License v2.0](https://www.gnu.org/licenses/gpl-2.0.txt), [SDL2](https://libsdl.org) licensed under the [zlib license](https://www.zlib.net/zlib_license.html).

## More Information:
- [OBS resource page](https://obsproject.com/forum/resources/input-overlay.552/)
- [Config creation tool](https://univrsal.github.io/input-overlay/cct/)
- [Convert old *.ini presets to JSON (WIP)](https://univrsal.github.io/input-overlay/converter/)
