"""当前模型版离线验证入口。"""
from pathlib import Path
import subprocess,sys
raise SystemExit(subprocess.call([sys.executable,"-X","utf8",str(Path(__file__).resolve().parents[1]/"verification/run.py")]))
