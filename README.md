# ESP32 Gesture Voice — 1.2.2 正式版

定档日期：2026-11-06。经典ESP32上的手势辅助沟通与脑电无线控制实验系统。本次冻结当前源码，保留原来版本的Git历史及发布包。

## 本版实现

- 默认演示模式：画三角形显示并通过云端TTS播报“我想吃饭”，画Z播报“你们好啊”。两动作只参考MPU6050。
- `m`切换九键拼音模式：上下保持连续选择、右侧翻确定、顺时针分隔、逆时针回退，肌电握拳提交联想。支持固定演示短语、本地拼音候选校验和中文播报。
- 同Wi-Fi控制中心：浏览器访问OLED显示的设备IP，查看真实脑电、肌电趋势、IMU、识别状态与OLED内容；控制显示、扬声器、音量、演示/拼音模式。无需电脑串口桥接。
- IMU成功读取后立即发布独立快照；页面区分正常、等待、读取异常、过期及语音/联想暂停，不伪造数据填补空缺。
- 语音改为分块下载并播放，避免整段PCM申请大块连续内存失败。
- 脑电经HC-04接收；专注度>70连续两包发送on，<50连续两包发送off，50～70保持。执行板GPIO2跟随26，25与26互斥；当前执行板保留300ms PWM软启动。
- 保留500Hz肌电采样、三级LED、手机Wi-Fi/API配置及断电保存。

## 开始使用

1. 核心板打开 `GestureVoice/GestureVoice.ino`；执行板打开 `BrainReceiver/BrainReceiver.ino`。两块是独立程序，勿互相刷错。
2. ESP32 Dev Module，8MB Flash，PSRAM Disabled，分区8M with spiffs（3MB APP/1.5MB SPIFFS）。不是ESP32-S3/C3目标。
3. 依赖版本：Arduino ESP32 3.2.0-cn、U8g2 2.34.22、CheezsEMG 1.0.2、ArduinoJson 7.2.0；未验证其他组合。
4. 首次连接开放热点 `ESP32`，打开 `http://192.168.4.1` 填写Wi-Fi及API密钥。随后手机/电脑与核心板连接同一局域网，访问OLED显示的IP。
5. 上电先自然放松，等待就绪再做动作。USB串口115200，端口按本机实际选择；`m`切换模式，`p`查询状态，`e`查询脑电，`h`帮助。

[下载正式版源码ZIP](https://github.com/Rxdrea/esp32-gesture-voice/releases/tag/v1.2.2) · [使用说明](docs/USAGE.md) · [模块分工](docs/ARCHITECTURE.md) · [验证边界](docs/VALIDATION.md) · [定档清单](docs/RELEASE-1.2.2.md)

## 编译与维护

```powershell
powershell -ExecutionPolicy Bypass -File .\Compile.ps1 -Target sender
powershell -ExecutionPolicy Bypass -File .\Compile.ps1 -Target receiver
```

脚本仅编译，默认构建输出放在D盘；可用 `-BuildPath`、`-Cli`、`-ConfigFile`指定自己的路径。修改 `web/index.html` 后执行 `python -B tools/embed_page.py` 再编译。网页已内嵌，正常使用不需要运行Python服务。

## 发布范围与限制

公开包包含两端固件源码、已导出模型、网页和必要工具、许可与说明。`PrivateConfig.h`由空模板生成，真实配置只在设备网页输入。不包含原始生理/动作数据、私人对话、设备MAC、私人调试地址、个人备份或带凭据固件。

这是当前工程的正式归档，不代表所有实物场景已验收。识别仍可能误触或拒识；同步云端请求期间IMU会暂停；无线面板没有执行板的物理动作回执。云模型可用性取决于账号及服务商，代码当前使用 `qwen3.7-flash`、`cosyvoice-v3-flash` / `longxiaochun_v3`，本次没有发起云调用。

局域网控制中心无公网账号认证；现有HTTPS沿用setInsecure。仅在可信局域网使用，不要直接映射公网。原来版本的tests/verification保留用于追溯，未适配本版，不作为1.2.2验证入口。


---

## 原来版本：1.1.2及更早记录

以下原文保留追溯，不代表1.2.2当前参数或验收结果。

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
- [下载1.1.2公开版压缩包](https://github.com/Rxdrea/esp32-gesture-voice/releases/tag/v1.1.2-clean)

## 配置与隐私

`PrivateConfig.h` 是空模板；真实配置只在本地设备网页输入。私人调试服务器默认关闭。仓库不包含原始佩戴数据、密钥、个人对话、Wi-Fi 密码或本地备份压缩包。

只编译：`powershell -ExecutionPolicy Bypass -File .\Compile.ps1`。该脚本不上传、不打开串口。

当前模型离线验证入口：`python tests/run_all_tests.py`，环境和可选私人数据回放见验证文档。本次更新未执行该套件，不将历史测试结果作为当前版本全部通过的证据。

当前目录已清理旧规则头文件与旧六步测试；原来版本源码可从Git历史或旧发布找回。

## 原来版本

以下是 v1.0.0 首次公开版的说明，与当前1.1.2不同。以后旧版内容统一标注为“原来版本”，当前使用方法见本文上方及使用说明.txt。

<details>
<summary>展开原来版本：v1.0.0 首次公开版</summary>


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

</details>
