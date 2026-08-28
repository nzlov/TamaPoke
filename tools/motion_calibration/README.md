# 单板动作校准固件

这个目录是独立 Arduino sketch，不链接也不修改 TamaPoke 游戏固件。它只初始化
QMI8658，执行可选的陀螺仪静止校准，并将每次 3 秒的六轴原始数据缓存在 PSRAM；采样结束后
才通过 USB 串口分块导出。

## 构建与刷写

```bash
arduino-cli compile --profile esp32s3 tools/motion_calibration
arduino-cli compile --profile esp32s3 --upload -p /dev/ttyACM0 tools/motion_calibration
python3 tools/motion_calibration/test_capture.py
g++ -std=c++17 -Wall -Wextra -Werror -I. -Itools/motion_calibration \
  tools/motion_calibration/host/replay.cpp motion.cpp \
  -o /tmp/motion-replay
g++ -std=c++17 -Wall -Wextra -Werror -I. \
  tools/emu/tests/motion_test.cpp motion.cpp -o /tmp/motion-test
/tmp/motion-test
```

主机采集脚本需要 `pyserial` 和桌面通知程序 `notify-send`。真机确认开始记录后，脚本会等待
250 ms 保护期再弹出“开始动作”通知；操作者应以该通知为准，不能提前动作。数据文件必须放到
仓库外：

```bash
python3 -m pip install pyserial
python3 tools/motion_capture.py --port /dev/serial/by-id/<board> \
  --out /path/to/motion-A.tsv --session A --label idle \
  --expected reject --trials 15 --calibrate
python3 tools/motion_capture.py --port /dev/serial/by-id/<board> \
  --out /path/to/motion-A.tsv --session A --label throw-normal \
  --expected throw --trials 20
```

动作期间始终握紧板子并给 USB 线留出余量，不得真的松手抛出。每次采集结束后脚本会检查
样本序号、溢出和超过 60 ms 的采样间隔，再追加到 TSV。

## Flick-and-hold 手势

旧的单次 shake 和“抛动后立即自然回手”具有重叠的六轴时序，不能只靠调高阈值可靠区分。
生产检测器因此使用明确的动作契约：从舒适的起始姿态向前甩动，使设备姿态肉眼可见地改变约
60° 或更多，然后在终点保持至少 400 ms；看到采集完成或下一次准备提示后才能缓慢复位。
甩动后回到起始姿态不符合这个契约。

检测器在选择动作后的 200 ms 保护期记录起始重力方向，再寻找同时具有足够角运动、角速度峰值和
加速度变化的运动事件。事件结束后必须连续保持 300 ms 低角速度，且终点重力方向相对起点至少
改变 50°。这个终点姿态条件是 shake 与 flick-and-hold 的主要分界，不依赖某一根陀螺仪轴或
简单的正反向符号。

主机回放直接调用生产固件的 `ThrowGestureDetector`；`--debug` 额外输出事件诊断：

```bash
/tmp/motion-replay /path/to/motion-A.tsv
/tmp/motion-replay --debug /path/to/motion-C.tsv
```

旧 A/B 的 throw 没有按新契约采集，不能作为 flick-and-hold 正样本；其 reject 样本仍可用于
负向回归。

## 一块板子的串联计划

同一块板子无法验证板间差异，所以数据按重启和重新校准后的会话隔离，不能把随机拆分后的指标
当成泛化能力。板子带电池时，USB 重连不等于重启，必须使用硬复位，并确认出现新的固件启动行。

1. 会话 C 是新契约训练集：静止校准一次，采集 `shake-one`、`flick-hold-soft`、
   `flick-hold-normal` 各 10 次。`shake-one` 是一次完整往返；两个 flick 标签都必须保持终点。
2. 候选只能使用 A/B reject 样本和 C 调整；确定常量后冻结实现。
3. 硬复位并重新校准后采集会话 D：三个核心标签各 10 次，另采 `held-idle`、
   `grip-adjust`、`wrist-turn` 各 5 次。D 的门槛是 20 个 flick 至少识别 19 个、10 个
   `shake-one` 至多误报 1 个、另外 15 个负样本零误报。
4. 如果查看 D 后修改任何规则或常量，D 自动转为训练集，必须再建新会话 E 验证，不能继续声称
   D 是独立结果。

所有 reject 标签的 `--expected` 使用 `reject`，两个 `flick-hold-*` 标签使用 `throw`。如果某次
出现丢样、长间隔或动作未遵守契约，保留原始记录并在旁注文件中标记原因，再使用新的 trial
编号补采；不要静默删除或重编号。

固件协议为 `MOTION CAL`、`MOTION TRACE <session> <label> <trial> <expected>`、
`MOTION STATUS`、`MOTION DUMP <offset>` 和 `MOTION CLEAR`。完整数据在收到 `MOTION CLEAR`
前一直保留，因此主机读取失败后可以重新导出。

采集结束后，用生产固件的 `ThrowGestureDetector` 直接重放：

```bash
/tmp/motion-replay /path/to/motion-A.tsv
/tmp/motion-replay --debug /path/to/motion-C.tsv
```

逐条输出的字段为 `TRIAL session label trial expected predicted samples dropped firedAt`；末行
`SUMMARY TP TN FP FN` 汇总混淆结果。
