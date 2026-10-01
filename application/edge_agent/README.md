# 九键键盘固件使用说明

本目录是 [ESP-CLAW-Keyboard](../../README.md) 的 ESP-IDF 固件应用。项目使用 ESP32-S3 N16R8 开发板，通过 USB HID 实现九键输入，并沿用 [ESP-Claw](https://github.com/espressif/esp-claw) 的联网、Web 配置和微信消息通路。

## 当前功能与按键编号

九键 USB 输入已在样机上验证，设备接收微信消息也已由用户确认。九个物理按键固定编号如下：

    1号  2号  3号
    4号  5号  6号
    7号  8号  9号

当前各键分别输入数字 1–9。编号表示物理位置，今后修改字符或宏时仍保留该编号。固件 AI 的系统提示词已经包含这张编号表；烧录包含该修改的新固件后，AI 可以理解用户说的“1号键”等指代。

微信修改宏的 AI 接口、宏配置保存和本地执行逻辑已实现并编译通过，待样机测试。支持普通键、组合快捷键、最多16步顺序宏、读取及恢复默认键位；详见 [宏配置说明](components/keyboard_hid/README.md)。旋钮和 RGB 灯暂未实现。

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

连接到局域网后，控制台地址以启动日志中的实际 IP 为准，不能固定沿用上次分配的地址。微信消息已能到达设备。新固件提供 cap_keyboard 功能组及 keyboard_set_macro、keyboard_get_config、keyboard_reset_key 三个工具。若在控制台手动设置过功能组白名单，需要启用 cap_keyboard 并对 AI 可见；AI 服务需支持工具调用。

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

## 微信回复与用量统计

处理指令时只发一条 `...`，后续阶段说明、工具轮次和英文思考内容不再发到微信。处理结束后正常回复，并在末尾附上本次 token 用量。

用量来自服务商响应的 `usage`，累加当前主代理请求的所有模型轮次（包括工具调用轮次），显示输入、输出和总数。上下文也是输入的一部分；统计不包含独立子代理、后台记忆提取、图片专用推理等额外调用。接口未返回用量时显示未知；仅部分轮次返回时标为已知部分，不能视作完整账单。

费用由固件按单价估算，不由 AI 编造。在 ESP-IDF 的 SDK 配置编辑器中找到 **WeChat reply usage and cost**，填写精确模型名称、每百万输入/输出 token 的价格和币种后重新编译应用。未填写、价格无效或切换成其他模型时显示“未配置当前模型价格”。单价允许填 0 表示免费。估算使用普通输入/输出价格，不区分缓存优惠、缓存写入溢价或服务商折扣，实际扣费以服务商账单为准。

样机验收：发送一条需要修改宏的指令，应只出现一次 `...`，结束后显示结果和用量；再次发送应再次显示一次 `...`。对照服务商多轮调用记录核对总数，并检查未返回 usage 和未配置价格时的提示。此功能已完成代码，实际微信显示和费用仍需烧录后验证。

### 通过微信切换 DeepSeek 模型

已有 DeepSeek 地址和密钥时，发送 `/llm model deepseek-flash` 切换模型，发送 `/llm status` 查看配置。首次接入可私聊发送 `/llm setup deepseek <你的API密钥> deepseek-flash`，该命令会配置并保存后端、地址、模型和密钥。需要 DATA 中的 `im_llm_command` 路由规则仍启用。

此处是固件处理的命令，当前不支持用普通中文自动切换模型。仅改模型不会替换服务商地址或密钥。DeepSeek 预设已将模型更新为 `deepseek-flash`，并修正输出参数名为 `max_tokens`；现有运行配置需重新执行 setup 或在 Web 中修改相应字段。

官方接口资料：[Chat Completions](https://api-docs.deepseek.com/api/create-chat-completion/)。V4.1 Flash 使用的官方模型标识是 `deepseek-flash`；模型在云端执行，当前未使用真实账号验证接入。

### 在 ESP-IDF 中配置费用

1. 打开命令面板，选择 `ESP-IDF: SDK Configuration Editor`。
2. 搜索 `WECHAT_USAGE`，找到 `WeChat reply usage and cost`。
3. `WECHAT_USAGE_PRICE_MODEL`：填写当前模型标识，例如 `deepseek-flash`。
4. `WECHAT_USAGE_INPUT_PRICE` / `WECHAT_USAGE_OUTPUT_PRICE`：分别填写服务商公布的每百万输入（普通/缓存未命中）和输出 token 的单价；仅填数字。
5. `WECHAT_USAGE_CURRENCY`：按该价目表填写 `CNY` 或 `USD`，不会自动换算汇率。
6. 保存配置，重新编译，按前面的说明仅烧录应用。

获取 token 无须填写上述单价：只要接口响应带有 `usage` 就自动统计。单价仅用于费用估算；不要把输入与输出单价填反，也不要填写“每千 token”价格。
