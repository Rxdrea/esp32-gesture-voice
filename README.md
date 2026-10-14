# ESP32 Gesture Voice — 1.1.2 模型拼音版

经典 ESP32 + MPU6050 + 单通道肌电实现拼音输入、中文候选联想和语音播报。当前版本将本地分类模型接入拼音业务，并支持保持上下翘连续选择。

## 当前操作

| 动作 | 功能 | 识别输入 |
|---|---|---|
| 上翘保持 | 连续上翻字母或候选，回位停止 | MPU6050 |
| 下压保持 | 连续下翻字母或候选，回位停止 | MPU6050 |
| 顺时针画圈 | 插入拼音分隔符 `'` | MPU6050 |
| 逆时针画圈 | 回退／删除 | MPU6050 |
| 右侧翻 | 确认字母或候选并播报 | MPU6050 |
| 大致自然位握拳约1秒 | 提交拼音生成中文候选 | 肌电与姿态保护 |

五种手腕动作不依赖肌电佩戴；握拳需要正常佩戴。单通道肌电不能分辨具体手指接触。当前不再使用 `c/u/d/r/l/g` 六步校准；上电时先在自然位放松，`c` 可重新记录自然位。

## 打开与配置

1. Arduino IDE 打开 `GestureVoice/GestureVoice.ino`。
2. ESP32 Dev Module；Flash **8MB**；PSRAM **Disabled**；分区 **8M with spiffs (3MB APP/1.5MB SPIFFS)**。
3. 依赖：ESP32 Arduino 3.2.0-cn、U8g2 2.34.22、CheezsEMG 1.0.2、ArduinoJson 7.2.0。其他版本尚未验证。
4. 首次连接设备开放热点 `ESP32`，手机打开 `http://192.168.4.1`，填写 Wi-Fi 和百炼 API 密钥；保存后自动联网，断电保留。
5. 先输入拼音；握拳请求中文候选；上下选择；右侧翻确认播报。
6. USB 串口115200；`p` 查看版本和状态，`t` 切换诊断，`h` 查看帮助。

联想使用北京地域兼容接口及 `qwen3.7-flash`，请求设置 `enable_thinking=false`、`stream=false`。旧 `qwen-turbo` 已按客服确认下线；实际账号权限、额度与服务状态仍需用户验证。云服务可能产生费用。

- [接线与操作](docs/USAGE.md)
- [设计与模块分工](docs/ARCHITECTURE.md)
- [验证范围与已知限制](docs/VALIDATION.md)
- [更新记录](CHANGELOG.md)

## 配置与隐私

`PrivateConfig.h` 是空模板；真实配置只在本地设备网页输入。私人调试服务器默认关闭。仓库不包含原始佩戴数据、密钥、个人对话、Wi-Fi 密码或本地备份压缩包。

只编译：`powershell -ExecutionPolicy Bypass -File .\Compile.ps1`。该脚本不上传、不打开串口。

当前模型离线验证入口：`python tests/run_all_tests.py`，环境和可选私人数据回放见验证文档。本次更新未执行该套件，不将历史测试结果作为当前版本全部通过的证据。

## 历史版本说明（保留原文）

以下为原公开版说明，六步校准与旧动作映射只适用于历史版本；当前用法以上文为准。

# ESP32 Gesture Voice

基于经典 ESP32、单通道 sEMG 肌电和 MPU6050 手腕姿态识别的辅助沟通实验系统。

## 功能概览

- 六种本地动作：上翘、下压、两侧翻掌、短握确认、长握联想
- 拼音字母输入与本地选字
- 可选云端大模型联想和云端语音合成
- OLED 显示候选与状态
- MAX98357 + 喇叭进行 I2S 播报
- 个性化校准数据保存到 ESP32 内部存储

单通道肌电主要反映前臂肌肉发力、放松、握拳和佩戴状态，不能可靠识别具体哪两根手指接触。

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

真实配置通过设备配置页面保存到 ESP32 内部存储；`PrivateConfig.h` 仅保留空模板。

## 项目状态

系统已完成离线输入分发、采样动作、个性化姿势和容错测试。真实佩戴、供电、网络和喇叭效果仍需根据具体硬件继续实测。
