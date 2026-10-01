# ESP-CLAW-Keyboard

基于 **ESP32-S3** 和 [ESP-Claw](https://github.com/espressif/esp-claw) 的九键桌面键盘项目。当前样机通过 USB-C 作为电脑的 USB 键盘使用；项目目标是让用户通过微信发一句话，就能修改九个按键的快捷键或宏，无需重新编译固件。

> 当前是开发中的功能原型。**USB 九键输入和微信接收已在样机上验证；微信改宏接口、保存与执行逻辑已实现并编译通过，待烧录后进行端到端实测。**

## 当前可以做什么

- 3×3 九个按键通过板上的 USB-C 向电脑输入数字 **1–9**，已由用户在实物上验证。
- 固件扫描按键矩阵，完成消抖，并通过 USB HID 发送键盘按键。
- 沿用 ESP-Claw 的联网、Web 配置和微信消息通路；设备接收微信消息已由用户验证。

九个物理按键固定编号为 1–9，从左到右、从上到下排列；编号表示按键位置，不会随今后输出的字符或宏改变。当前已支持通过 AI 工具设置普通键、组合快捷键和顺序宏，并在 NVS 保存。旋钮和 RGB 灯暂未启用。[宏配置说明](application/edge_agent/components/keyboard_hid/README.md)包含接口、微信示例和样机验收步骤。

## 微信配置宏键盘

用户可以发送“把左上角改成 Ctrl+Shift+S”这样的微信消息。设备识别键位和动作，保存设置并回复结果；之后按左上角就发送对应快捷键，断电重连后仍然有效。

当前代码支持普通按键、组合快捷键和按顺序执行的按键宏，已保存的宏由设备本地执行。需要烧录新固件并完成样机验收后确认实际体验。打开电脑上任意安装的软件需要电脑知道该软件的位置，因此会另行评估 Windows 快捷方式热键或轻量电脑端程序。

## 硬件与代码

- 主控：ESP32-S3 N16R8 开发板；键盘 PCB 为 3×3 矩阵。
- 矩阵行：GPIO4、GPIO5、GPIO6；矩阵列：GPIO11、GPIO10、GPIO9；按键低电平触发。
- USB 键盘代码：[keyboard_hid.c](application/edge_agent/components/keyboard_hid/keyboard_hid.c)。
- 固件入口：[main.c](application/edge_agent/main/main.c)。
- 微信消息接收：[cap_im_wechat.c](components/claw_capabilities/cap_im_platform/src/cap_im_wechat.c)。

## 目前怎样使用

1. 将适配该 ESP32-S3 键盘的固件烧录到开发板，并通过板上的 USB-C 接入电脑。
2. 打开任意文本输入框，依次按九个键；当前默认输入为从左到右、从上到下的 **1–9**。
3. 如需使用设备的联网、Web 控制台和微信消息通路，按 [ESP-Claw 官方配置教程](https://esp-claw.com/zh-cn/tutorial/web-config/)完成配置。烧录包含宏接口的新固件后，可按宏配置说明发送改键消息；AI 服务需支持工具调用，并启用 cap_keyboard 功能组。

固件应用位于 `application/edge_agent`，使用 ESP-IDF 构建。本仓库的 `idf-claw.cmd` 是当前开发机的构建脚本，内含本机 ESP-IDF 和 Python 的绝对路径；在其他电脑使用前需要调整这些路径。ESP-Claw 的通用构建与配置说明见[官方文档](https://esp-claw.com/zh-cn/tutorial/)。

## 项目来源

本项目衍生自乐鑫的 [ESP-Claw 原仓库](https://github.com/espressif/esp-claw)。ESP-Claw 在这里提供设备应用、联网、Web 配置及微信消息等基础能力；本项目在此基础上开发九键矩阵扫描、USB HID 键盘和后续的微信可配置宏功能。ESP-Claw 的完整功能、支持板卡和通用使用方法请以[其官方文档](https://esp-claw.com/zh-cn/)为准。

本仓库保留原项目的 [Apache-2.0 许可证](LICENSE)。

微信回复已加入处理中 `...` 提示及本次 token/估算费用统计，使用与价格配置见[应用说明](application/edge_agent/README.md)。代码编译与实机验收的状态分别记录，微信显示仍需烧录验证。
