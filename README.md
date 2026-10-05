# ESP32 Gesture Voice

基于经典 ESP32、单通道 sEMG 肌电和 MPU6050 手腕姿态识别的辅助沟通实验系统。

## 功能概览

- 六种本地动作：上翘、下压、两侧翻掌、短握确认、长握联想
- 拼音字母输入与本地选字
- 可选云端大模型联想和云端语音合成
- OLED 显示候选与状态
- MAX98357 + 喇叭进行 I2S 播报
- 个性化校准数据保存到 ESP32 内部存储

单通道肌电主要反映前臂肌肉发力、放松、握拳和佩戴状态，不能可靠识别具体哪两根手指接触。本项目是毕业设计实验联调版本，不是医疗或紧急通信设备。

## 硬件接线

| 模块 | ESP32 引脚 |
|---|---|
| sEMG 模拟信号 | GPIO34 |
| sEMG 佩戴检测 | GPIO2 |
| MPU6050/OLED SDA、SCL | GPIO21、GPIO22 |
| MAX98357 BCLK/LRC/DIN | GPIO26、GPIO25、GPIO27 |
| HC-04 Serial2 | GPIO16、GPIO17，57600 |
| 配置重置按钮 | GPIO33 接 GND，内部上拉 |

## 使用方法

1. 用 Arduino IDE 打开 `GestureVoice/GestureVoice.ino`。
2. 将 `GestureVoice/PrivateConfig.h` 中的本地配置填写到设备网页，或保持为空进行离线校准和本地选字。
3. 目标板选择 ESP32 Dev Module，Flash 8MB，PSRAM Disabled。
4. 首次启动按 OLED 提示连接配置热点 `ESP32`，打开 `192.168.4.1` 填写 Wi-Fi 和云服务密钥。
5. 按 `c/u/d/r/l/g` 完成六步校准。
6. 运行测试：

```bash
python tests/run_all_tests.py
```

## 隐私说明

请勿把真实 Wi-Fi 密码、云服务 API 密钥、SSH 私钥、Token 或服务器内部路径提交到公开仓库。真实配置通过设备配置页面保存到 ESP32 内部存储；`PrivateConfig.h` 仅保留空模板。

## 项目状态

系统已完成离线输入分发、采样动作、个性化姿势和容错测试。真实佩戴、供电、网络和喇叭效果仍需根据具体硬件继续实测。
