# 1.2.2当前模块职责

| 文件 | 作用 |
| --- | --- |
| GestureVoice.ino | 模式、九键拼音/候选、NVS配置、LLM、流式PCM/TTS、输入分发 |
| MlGestureSensor.h | MPU读取、独立IMU快照、识别协调、分片OLED、诊断 |
| DemoFeatures.h / DemoRecognizer.h / DemoModel.h | 三角形/Z六轴特征、DTW拒识与模板 |
| DemoDisplay.h | OLED演示界面 |
| HoldScroll.h / WristMotionGate.h / WristModel.h | MPU连续上下翻与其他手腕分类 |
| EmgRecognition.h / FusionCore.h / FusionFeatures.h / GripModel.h / MotionGuardModel.h | 500Hz肌电、LED、握拳与运动抑制 |
| PinyinGuard.h / PinyinTable.h | 本地拼音候选过滤，第三方字音许可见third_party |
| InputRouter.h | 业务输入与忙态保护 |
| BrainDecision.h / BrainRemote.h / BrainProtocol.h | 新脑电包判断、UART后台、ESP-NOW协议 |
| ControlCenter.h / ControlCenterPage.h | HTTP后台、锁保护快照/控制队列、内嵌网页 |
| web/index.html / tools/embed_page.py | 网页源文件与头文件生成 |
| BrainReceiver/BrainReceiver.ino | 无线接收、自动找信道、GPIO互斥与软启动 |
| PrivateConfig.h | 发布包空模板；真实配置由设备网页写NVS |

网页HTTP后台不直接访问I2C/ADC，主循环通过队列执行控制。IMU成功读取后立即发布小快照；肌电独立500Hz任务、脑电独立UART任务。同步LLM/TTS仍占用主循环，不能声称全链路无阻塞。语音按小块PCM播放，不再整段malloc。

旧GestureSensor.h/GestureCore.h不属于活动代码；当前保留的模型文件仍用于原拼音模式，不应因为默认演示模式暂时不用就删除。tests/verification属于原来1.1.2，未适配1.2.2，保留追溯。


---

## 原来版本：1.1.2及更早记录

以下原文保留追溯，不代表1.2.2当前参数或验收结果。

# 设计与模块分工

- `GestureVoice.ino`：拼音与候选界面、手机配置、NVS保存、按钮重置、联想HTTP、语音合成与I²S播放。
- `MlGestureSensor.h`：当前活动入口；MPU读取、模型动作、握拳与连翻协调、OLED分片更新及诊断。
- `HoldScroll.h`：只使用MPU的上下保持连翻状态机；进入、暂停、重复、回位退出。
- `EmgRecognition.h`：独立500Hz肌电采样与三级LED，不在采样任务里操作OLED/I²C。
- `WristModel.h`、`MotionGuardModel.h`、`GripModel.h`：本地模型权重；不是云端聊天模型。
- `FusionCore.h`、`FusionFeatures.h`、`WristMotionGate.h`：特征、握拳判断和运动保护。
- `InputRouter.h`：手势和串口统一操作入口，网络业务期间禁入。
- 旧 `GestureSensor.h`、`GestureCore.h` 和配套旧测试已从当前目录删除；原来版本可从Git历史和旧发布获取。

## 多任务与忙态

肌电独立任务继续采样，主循环处理MPU、OLED和操作。网络和播放期间不积攒动作；结束后重新放松。连翻活动时清除旧握拳与分类缓存，避免一次动作同时触发联想、确认或两次选择。

模型工作数组改为长期存储并采用原位reset；主循环任务栈16KB。OLED文字排队且分片传输，避免每次循环整屏阻塞。

## 公开版与本机版

公开版源于本机1.1.2，功能逻辑同步；区别是PrivateConfig为空，私人调试服务器地址为空且日志发送函数有关闭保护。训练数据与采集CSV不公开，模型权重随源码提供。
