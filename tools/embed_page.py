"""把实际网页嵌入ESP32 Flash；不改变页面内容，不联网。"""
from pathlib import Path
import hashlib
root = Path(__file__).resolve().parents[1]
page = root / 'web' / 'index.html'
text = page.read_text(encoding='utf-8')
if ')CC122"' in text:
    raise SystemExit('HTML包含保留的C++字符串结束符')
header = '#pragma once\n#include <Arduino.h>\n// 由tools/embed_page.py生成，源文件为web/index.html。\nstatic const char CONTROL_CENTER_HTML[] PROGMEM = R"CC122(' + text + ')CC122";\n'
(root / 'GestureVoice' / 'ControlCenterPage.h').write_text(header, encoding='utf-8')
print(f'PAGE_BYTES={len(text.encode("utf-8"))} SHA256={hashlib.sha256(text.encode("utf-8")).hexdigest()}')