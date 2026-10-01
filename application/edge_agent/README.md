# 九键键盘固件使用说明

本目录是 [ESP-CLAW-Keyboard](../../README.md) 的 ESP-IDF 固件应用。项目使用 ESP32-S3 N16R8 开发板，通过 USB HID 实现九键输入，并沿用 [ESP-Claw](https://github.com/espressif/esp-claw) 的联网、Web 配置和微信消息通路。

## 当前功能与按键编号

九键 USB 输入已在样机上验证，设备接收微信消息也已由用户确认。九个物理按键固定编号如下：

    1号  2号  3号
    4号  5号  6号
    7号  8号  9号

当前各键分别输入数字 1–9。编号表示物理位置，今后修改字符或宏时仍保留该编号。固件 AI 的系统提示词已经包含这张编号表；烧录包含该修改的新固件后，AI 可以理解用户说的“1号键”等指代。

微信修改宏、宏配置保存、旋钮和 RGB 灯暂未实现。知道按键编号不等于能够通过聊天修改按键动作。

矩阵行使用 GPIO4/5/6，列使用 GPIO11/10/9，按键低电平触发。扫描与消抖由键盘任务执行，USB 发送数字键。

## 两个 Type-C 口怎样使用

以下区分以当前开发板 ESP32-S3-SCH.pdf 原理图为准，USB1/USB2 是原理图的器件编号，板上标字可能不同。

| 接口 | 接线 | 当前用途 |
| --- | --- | --- |
| USB1：USB 转串口口 | USB → CH343P → ESP32-S3 UART0，并带自动下载电路 | 烧录和串口日志 |
| USB2：ESP32-S3 原生 USB 口 | USB → GPIO19（D−）/GPIO20（D+） | 作为 USB 键盘连接电脑 |

日常输入和按键测试使用 USB2。烧录或查看日志优先把数据线接到 USB1，电脑应识别 CH343P 对应的 COM 端口；如果没有识别，先检查数据线和 CH343 驱动。不能按接口的左右位置判断，应以板上标识和串口识别结果确认。

当前固件将原生 USB 用作 HID 键盘，没有提供 USB CDC 串口，且已关闭辅助 USB Serial/JTAG 控制台。因此，在 USB2 上运行键盘固件后，原先的串口可能消失；这不代表开发板损坏。UART0 日志仍保留，波特率为 115200，可从 USB1 查看。

也可以在 USB2 上手动进入 ROM 下载模式：按住 BOOT，按一下 RST/RESET，再松开 BOOT，然后重新查看端口。下载模式期间按键输入不可用；烧录后正常启动，原生 USB 再作为键盘使用。

## Windows 开发环境与构建

当前工程使用 ESP-IDF 5.5.4，选用的板级配置是 [esp32_s3_n16r8](boards/community/esp32_s3_n16r8/)。构建输出位于仓库根目录的 build/keyboard，而不是本目录的 build。

在仓库根目录运行：

    .\idf-claw.cmd build

该脚本内含当前开发机的 ESP-IDF 和 Python 绝对路径。在另一台电脑使用前，需要调整脚本中的路径并安装对应开发环境。已经配置好的本机工程无需为了普通构建重新生成板级配置。

新开发环境的板级生成方法：

    cd application/edge_agent
    idf.py bmgr -c ./boards -b esp32_s3_n16r8
    idf.py build

执行前需安装并启用 ESP-IDF 环境及 esp-bmgr-assist。通用安装与构建步骤见 [ESP-Claw 官方文档](https://esp-claw.com/zh-cn/reference-project/build-from-source/)。

## 烧录和日志

先连接 USB1，在设备管理器中确认实际 COM 端口。下列 COMx 是占位符，需要替换为真实端口。在仓库根目录查看日志：

    .\idf-claw.cmd -p COMx monitor

固件更新有两种不同范围：

| 方式 | 写入内容 | 使用条件 |
| --- | --- | --- |
| app-flash | 仅应用固件；当前构建写入 ota_0，偏移 0x20000 | 设备已有相同分区布局，且启动的是 ota_0 |
| flash | 应用、引导程序、分区表、OTA 初始数据、system 和 storage 镜像 | 首次部署或确认需要全量重新部署 |

app-flash 不写入 NVS 和 storage 分区，因此保留其中的微信配置和运行数据。满足上表的应用更新条件时，可使用：

    .\idf-claw.cmd -p COMx app-flash

若设备通过 OTA 切换到 ota_1，仅执行 app-flash 不会让新写入的 ota_0 自动成为启动分区。不能确认分区和当前启动槽时，应先核对，再选择更新方式。

首次全量部署可使用以下命令，但会写入 storage.bin，覆盖可写数据分区中的现有文件。更新已有微信配置的设备前，需要先备份并确认更新范围。

    .\idf-claw.cmd -p COMx flash

烧录后接回 USB2，在文本输入框中逐个按下九键，确认输入 1–9。再通过微信询问按键编号，验证 AI 的新说明是否随固件生效。编译成功和实际烧录、按键测试是不同的验证步骤。

## Web 配置与微信

设备联网、Web 控制台和微信绑定沿用 ESP-Claw 的配置流程，参考 [官方 Web 配置教程](https://esp-claw.com/zh-cn/tutorial/web-config/)。

连接到局域网后，控制台地址以启动日志中的实际 IP 为准，不能固定沿用上次分配的地址。微信消息已能到达设备，当前宏配置功能仍待开发。

## 代码与存储目录

- [components/keyboard_hid/keyboard_hid.c](components/keyboard_hid/keyboard_hid.c)：矩阵扫描、固定编号和 USB HID 报告。
- [main/main.c](main/main.c)：固件启动流程和键盘初始化。
- [app_claw.c](../../components/common/app_claw/app_claw.c)：设备 AI 的系统提示词，包含九键编号规则。
- [cap_im_wechat.c](../../components/claw_capabilities/cap_im_platform/src/cap_im_wechat.c)：微信消息接收与发送。
- [partitions_16MB.csv](partitions_16MB.csv)：16 MB Flash 分区布局。

文件系统源文件位于：

    fatfs_image/
    ├── storage/   可写数据分区的初始内容
    └── system/    只读系统分区的基础内容

构建时分别暂存到仓库根目录的 build/keyboard/fatfs_image 和 build/keyboard/system_fs_image，并生成 storage.bin、system.bin。选中板卡目录下可选的 fatfs_image 内容会覆盖到 system 镜像中，同路径文件以板卡版本为准；技能和内置 Lua 资源也会同步到 system 镜像。

系统分区挂载到 /system。可写数据根目录由 claw_paths 在启动时确定，可能来自 Flash 或 SD 卡；开发功能时应通过 CLAW_PATH_DATA 获取路径，避免在可复用代码中固定写死 /fatfs。
