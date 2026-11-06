# Wi-Fi控制中心（1.2.2正式版）

当前操作、数据含义和限制统一见[使用说明](USAGE.md)。源页面为web/index.html；生成头文件为GestureVoice/ControlCenterPage.h。运行时由ESP32自身提供，不需要PC服务或串口桥接。

```powershell
python -B tools/embed_page.py
python -B tools/preview.py
```

preview仅用于电脑布局预览，显示未连接硬件，不生成模拟传感器值。真实界面访问核心板OLED显示的IP。IMU修复已包含在正式源码；当前重新编译、历史上传及未完成实物验收见[验证记录](VALIDATION.md)。
