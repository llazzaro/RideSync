# Contributing

Work in small milestone-focused branches and submit GitHub pull requests.
Use the setup/build/check commands in README before proposing a change.

Hardware changes need exact board and modem revisions. Protocol changes need
primary source links, applicable licenses, packet fixtures and evidence labels.
Do not copy sources without permission or assume an absent license permits reuse.
Never mark a write/notify success as confirmed camera state. Include deadlines,
per-camera error handling and tests for malformed input and disconnects.

Add only modules required by the active milestone; keep pure codecs and state
machines testable without hardware. Before expanding BLE dependencies, measure
connection capacity and decide whether PlatformIO/Arduino remains appropriate.
Attach manual test logs and camera firmware versions when claiming support.
