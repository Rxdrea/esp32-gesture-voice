"""Build/run the real merged sketch offline in a credential-free ASCII directory.
Usage: python tests/host_stubs/run_merged_tests.py [--case CASE] [--scratch DIR]
Only allowlisted application sources are copied; PrivateConfig.h is NEVER read.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--scratch', type=Path)
    parser.add_argument('--case')
    parser.add_argument('--compiler', type=Path, default=Path('E:/Software/CLion 2024.1.1/bin/mingw/bin/g++.exe'))
    parser.add_argument('--arduino-json', type=Path, default=Path.home() / 'Documents/Arduino/libraries/ArduinoJson/src')
    args = parser.parse_args()
    sources = ('GestureVoice.ino', 'GestureSensor.h', 'GestureCore.h', 'InputRouter.h')
    missing = [name for name in sources if not (args.source / name).is_file()]
    if missing:
        print('Source not ready; no compile attempted: ' + ', '.join(missing), file=sys.stderr)
        return 2
    if not args.compiler.is_file() or not (args.arduino_json / 'ArduinoJson.h').is_file():
        print('Compiler or installed ArduinoJson not found.', file=sys.stderr)
        return 2
    base = Path(tempfile.gettempdir()) / 'esp32-gesture-voice-tests'
    base.mkdir(parents=True, exist_ok=True)
    scratch = args.scratch or Path(tempfile.mkdtemp(prefix='new-merged-tests-', dir=base))
    scratch.mkdir(parents=True, exist_ok=True)
    for name in sources:
        shutil.copy2(args.source / name, scratch / name)
    tests = scratch / 'tests'
    tests.mkdir(exist_ok=True)
    shutil.copy2(args.source / 'tests/merged_app_tests.cpp', tests / 'merged_app_tests.cpp')
    shutil.copytree(args.source / 'tests/host_stubs', tests / 'host_stubs', dirs_exist_ok=True)
    shutil.copytree(args.arduino_json, scratch / 'ArduinoJson', dirs_exist_ok=True)
    (scratch / 'PrivateConfig.h').write_text(
        '#pragma once\n'
        'const char* WIFI_SSID = "host-only-no-network";\n'
        'const char* WIFI_PASS = "host-only-not-a-password";\n'
        'const char* API_KEY = "host-only-not-an-api-key";\n', encoding='utf-8')
    exe = scratch / 'merged_app_tests.exe'
    command = [str(args.compiler), '-std=gnu++17', '-Wall', '-Wextra',
               '-I' + str(tests / 'host_stubs'), '-I' + str(scratch / 'ArduinoJson'),
               str(tests / 'merged_app_tests.cpp'), '-o', str(exe)]
    print('Credential-free test copy: ' + str(scratch), flush=True)
    env = os.environ.copy()
    env['PATH'] = str(args.compiler.parent) + os.pathsep + env.get('PATH', '')
    result = subprocess.run(command, env=env)
    if result.returncode:
        return result.returncode
    return subprocess.run([str(exe)] + ([args.case] if args.case else []), env=env).returncode


if __name__ == '__main__':
    sys.exit(main())
