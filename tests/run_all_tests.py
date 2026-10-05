"""Run every shipped host suite offline; no credentials, serial port or cloud access.
Usage: python tests/run_all_tests.py
Requires the local MinGW compiler and installed ArduinoJson used for this build.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile


def main():
    root = Path(__file__).resolve().parents[1]
    compiler = Path('E:/Software/CLion 2024.1.1/bin/mingw/bin/g++.exe')
    if not compiler.is_file():
        print('MinGW compiler not found: ' + str(compiler), file=sys.stderr)
        return 2
    env = os.environ.copy()
    env['PATH'] = str(compiler.parent) + os.pathsep + env.get('PATH', '')
    work = Path(tempfile.mkdtemp(prefix='gesture-voice-all-tests-'))
    (work / 'tests').mkdir()
    (work / 'GestureVoice').mkdir()
    for name in ('GestureCore.h', 'InputRouter.h'):
        shutil.copy2(root / 'GestureVoice' / name, work / 'GestureVoice' / name)
    for name in ('input_router_tests.cpp', 'gesture_tests.cpp',
                 'personalized_pose_tests.cpp', 'chinese_ui_tests.cpp',
                 'tolerance_tests.cpp'):
        shutil.copy2(root / 'tests' / name, work / 'tests' / name)
    suites = [
        ('router', 'input_router_tests.cpp', ['-DINPUT_ROUTER_WITH_GESTURE_CORE']),
        ('router_no_exceptions', 'input_router_tests.cpp',
         ['-DINPUT_ROUTER_WITH_GESTURE_CORE', '-fno-exceptions', '-fno-rtti']),
        ('gesture', 'gesture_tests.cpp', []),
        ('personalized_pose', 'personalized_pose_tests.cpp', []),
        ('chinese_ui', 'chinese_ui_tests.cpp', []),
        ('tolerance', 'tolerance_tests.cpp', []),
    ]
    failed = []
    for name, file, flags in suites:
        exe = work / (name + '.exe')
        command = [str(compiler), '-std=c++11', '-Wall', '-Wextra', '-Werror',
                   '-pedantic', '-finput-charset=UTF-8', '-fexec-charset=UTF-8',
                   *flags, str(work / 'tests' / file), '-o', str(exe)]
        print('\nBUILD ' + name, flush=True)
        result = subprocess.run(command, env=env)
        if result.returncode:
            failed.append(name + ':compile')
            continue
        result = subprocess.run([str(exe)], env=env, timeout=60)
        if result.returncode:
            failed.append(name + ':run')
    print('\nBUILD merged application', flush=True)
    result = subprocess.run([
        sys.executable,
        str(root / 'GestureVoice/tests/host_stubs/run_merged_tests.py'),
    ], env=env, timeout=120)
    if result.returncode:
        failed.append('merged_application')
    print('CORE_TEST_COPY=' + str(work), flush=True)
    print('FAILED=' + repr(failed), flush=True)
    return int(bool(failed))


if __name__ == '__main__':
    sys.exit(main())
