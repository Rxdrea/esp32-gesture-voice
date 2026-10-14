"""当前模型版离线验证入口；旧六步版本见 run_legacy_tests.py。"""
from pathlib import Path
import subprocess,sys
raise SystemExit(subprocess.call([sys.executable,str(Path(__file__).resolve().parents[1]/"verification/run.py")]))
