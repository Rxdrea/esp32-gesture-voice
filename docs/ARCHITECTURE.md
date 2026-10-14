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
