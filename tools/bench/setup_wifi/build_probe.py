"""Explicit compile/link trial. Run only while holding the shared PlatformIO slot."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
script = ROOT / "tools/bench/setup_wifi/add_probe.py"
config = (ROOT / "platformio.ini").read_text().replace(
    "[platformio]", "[platformio]\nbuild_dir = .pio/setup_wifi_trial", 1
) + f"""
[env:setup_wifi_probe]
extends = env:serial_setup_application
extra_scripts =
  ${{env:serial_setup_application.extra_scripts}}
  pre:{script}
build_flags =
  ${{env:serial_setup_application.build_flags}}
  -Wl,-u,ridesync_setup_wifi_probe
"""
with tempfile.NamedTemporaryFile(mode="w", suffix=".ini", dir=ROOT) as trial:
    trial.write(config)
    trial.flush()
    subprocess.run(["pio", "run", "-d", str(ROOT), "-c", trial.name,
                    "-e", "setup_wifi_probe"], check=True)
