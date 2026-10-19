# 九键 USB 宏配置

本组件提供矩阵扫描、USB 键盘输入、宏配置保存及设备 AI 可调用的工具。微信消息进入 ESP-Claw 后，AI 根据用户指令调用这些工具；不需要电脑常驻程序。宏执行在设备本地进行，配置消息需要联网和可用的 AI 服务。

当前实现已经编译通过；微信实际调用、USB 快捷键效果和断电恢复仍需在样机上测试。

## 固定编号

    1 2 3
    4 5 6
    7 8 9

编号表示物理位置。出厂默认按键分别输入数字 1–9，修改输出后编号仍不变。

## 可以对微信说什么

- “把1号键设为 Ctrl+C。”
- “把2号键改成先 Ctrl+A，再等待100毫秒，然后 Ctrl+C。”
- “查看所有按键现在的设置。”
- “恢复1号键的默认设置。”
- “恢复全部九个键的默认设置。”

普通键和单个组合快捷键使用 shortcut 模式，按住实体键时保持按下，松开时释放。顺序动作使用 macro 模式，每次新的实体按下执行一次；持续按住不会重复启动。AI 必须等接口返回 ok=true 后才能回复已保存，不能凭聊天回答假装改键。

## AI 调用接口

### keyboard_set_macro

修改一个键，校验参数，保存到 NVS，随后按键使用新设置。输入示例：

```json
{"key_id":1,"mode":"shortcut","steps":[{"keys":["CTRL","C"]}]}
```

多步宏：delay_ms 是该步松开后到下一步的等待时间。

```json
{"key_id":2,"mode":"macro","steps":[{"keys":["CTRL","A"],"delay_ms":100},{"keys":["CTRL","C"]}]}
```

- key_id：整数 1–9。
- mode：shortcut 或 macro。shortcut 只能有一步，且不带非零延时。
- steps：1–16 步；每一步最多六个普通键，外加 CTRL、SHIFT、ALT、WIN。
- delay_ms：整数 0–10000，省略则为 0。macro 每一步按下约30毫秒后释放。
- 支持 A–Z、0–9、F1–F12、Enter、Esc、Tab、Space、Backspace、Delete、Insert、Home、End、PageUp、PageDown、方向键和描述中列出的标点键、CapsLock、PrintScreen、ScrollLock、Pause。
- 字母名称代表键盘键位；字符结果受电脑键盘布局、输入法和修饰键影响。当前不提供任意中文文本输入、鼠标动作或启动本机任意软件。

成功结果包含 ok=true、persisted=true 和实际写入的键位设置；参数或保存失败会返回 ok=false，原有内存配置不替换。

### keyboard_get_config

输入 {"key_id":1} 读取一个键，输入 {} 读取全部九键。返回实际配置、行列位置和执行步骤，AI 不应根据编号猜测当前输出。

### keyboard_reset_key

输入 {"key_id":1} 恢复该键的默认数字，输入 {} 恢复全部九键。只清除 keyboard 命名空间中的宏设置，不修改微信、Wi-Fi 或其他配置。

以上工具属于 cap_keyboard 功能组，默认启用并对 AI 可见。如果曾在 Web 控制台手动设置功能组白名单，需要同时把 cap_keyboard 加入已启用及 AI 可见的功能组。AI 服务必须支持工具调用。

## 运行与保存规则

- 配置保存在 NVS 的 keyboard 命名空间，启动时加载。配置在未擦除 NVS 的情况下断电保留。
- 宏按物理新按下事件入队，按顺序执行，最多等待九次触发。正在执行的宏使用启动时的配置快照。
- 宏播放时暂停普通快捷键上报；结束后恢复仍按住的普通键。宏播放中短按并松开的普通键可能没有输出，应避免在长宏期间混按普通键。
- USB 断开时取消当前与等待中的宏；重新连接后需要重新按下宏键。
- 扫描任务保持周期运行。宏只推进到时的动作，不通过长时间休眠阻塞扫描；USB 报告发送成功后才推进按下和释放阶段。
- 多个普通快捷键同时按下时合并报告，超过六个不同普通键会发送 HID rollover。
- 微信用户绑定权限仍沿用现有消息通路，本组件尚未增加产品级设备归属或修改者白名单。

## 烧录与样机验收

新增逻辑和 AI 工具都在应用固件内，无需更新 system 或 storage 镜像。沿用 [应用 README](../../README.md) 中的 app-flash 条件：保持现有分区，确认启动 ota_0，再仅烧录应用，保留微信数据。

1. 烧录后保持微信登录，询问所有键位，应得到初始数字或此前保存的配置。
2. 将1号键设置为 Ctrl+C，在电脑上选择文本后按1号键，用普通键盘 Ctrl+V 验证剪贴板。
3. 将2号键设置为 Ctrl+A → 等待100毫秒 → Ctrl+C，用文本编辑器验证顺序动作。
4. 配置一个两步数字序列，持续按住应只执行一次；释放再按应再执行一次。
5. 断电重启后查询配置，再验证快捷键，确认宏和微信配置仍保留。
6. 恢复单键和全部默认值，确认重新输入相应数字。
7. 宏执行中拔掉 USB，再连接，确认没有卡住修饰键或自动重播。

## 微信回复与用量统计

处理指令时只发一条 `...`，后续阶段说明、工具轮次和英文思考内容不再发到微信。处理结束后正常回复，并在末尾附上本次 token 用量。

用量来自服务商响应的 `usage`，累加当前主代理请求的所有模型轮次（包括工具调用轮次），显示输入、输出和总数。上下文也是输入的一部分；统计不包含独立子代理、后台记忆提取、图片专用推理等额外调用。接口未返回用量时显示未知；仅部分轮次返回时标为已知部分，不能视作完整账单。

费用由固件按单价估算，不由 AI 编造。在 ESP-IDF 的 SDK 配置编辑器中找到 **WeChat reply usage and cost**，填写精确模型名称、每百万输入/输出 token 的价格和币种后重新编译应用。未填写、价格无效或切换成其他模型时显示“未配置当前模型价格”。单价允许填 0 表示免费。估算使用普通输入/输出价格，不区分缓存优惠、缓存写入溢价或服务商折扣，实际扣费以服务商账单为准。

样机验收：发送一条需要修改宏的指令，应只出现一次 `...`，结束后显示结果和用量；再次发送应再次显示一次 `...`。对照服务商多轮调用记录核对总数，并检查未返回 usage 和未配置价格时的提示。此功能已完成代码，实际微信显示和费用仍需烧录后验证。

## 按键观测灯

开发板原理图 ESP32-S3-COREBOARD V1.4 中，板载 WS2812B RGB 灯由 GPIO48 驱动。任意矩阵按键经消抖确认按住后，板载灯以 20% 驱动亮度亮绿灯（G=51/255）；所有按键松开后熄灭。沿用约 20 ms 的按键消抖，没有定时闪灯或额外延迟熄灭。

观测的是 USB 上报和宏执行之前的实际矩阵状态，不依赖 Wi-Fi、AI 或 USB 是否连接。排查时逐个单独按键：若亮灯但电脑无输出，继续检查宏配置及 USB；若其他键能亮灯而该键不亮，说明扫描尚未检测到该键按住。键盘 PCB 上 GPIO7 的 RGB 灯链与板载灯不同。观测期间请勿另用 Lua 或其他任务驱动 GPIO48。

当前功能需要烧录新应用后进行实机验证，编译通过不代表已验证实际灯光。

## 底板逐键灯与编码器

底板 RGB 使用 GPIO7，按 WS2812 兼容的 24 位 GRB 灯链驱动。以 2026-09-29 保存的原理图为准，串联顺序为 U7 → U8 → U9 → U4 → U5 → U6 → U1 → U2 → U3，分别对应从左到右、从上到下的按键 1–9；旧网表的顺序不同，实机若灯位不符需核对实际 PCB 版本并调整 keyboard_board.h 的映射。九灯默认绿灯约 10% 驱动亮度（G=26/255）；按住哪个键，其下方灯熄灭，松开恢复，支持多个键同时按下。灯光跟随消抖后的实际矩阵状态，不依赖宏、AI 和 USB。

EC11E15244G1 编码器 A/B 为 GPIO12/13，公共端接地。旋转通过独立的 USB Consumer Control HID 接口发送系统音量增减。按键接在 ROW1/GPIO4 与 COL4/GPIO8，通过同一矩阵扫描消抖；每次新的按下发送一次静音键，Windows 切换静音/取消静音，持续按住不重复切换。编码器按键不是第十个可配置宏键。旋转使用 GPIO 双边沿中断解码，按该型号 30 定位/15 脉冲处理；转向可用 keyboard_board.h 的 KEYBOARD_BOARD_ENCODER_DIRECTION 反转。

普通键盘仍使用原来的 HID 接口和报告，新增独立多媒体接口，两者分别发送。多媒体事件仅在 USB 报告提交成功后出队，并发送释放报告；USB 断开时丢弃待发送事件，防止重连后补发。底板灯与原有 GPIO48 板载观测灯分别控制；勿用 Lua/其他驱动同时占用 GPIO7、GPIO12/13。固件更新后重新插拔原生 USB，使电脑重新识别两个 HID 接口。

验收：逐个按 1–9，确认仅对应灯熄灭；同时按多键后分别松开；编码器左右各慢转数格，核对系统音量方向与每格触发；按住编码器应仅切换一次静音，松开再按应恢复；保持九键输入并操作编码器，检查输入与音量互不干扰。当前尚未烧录验证灯位、颜色及旋转方向。

### RMT DMA transport

The 9-LED GPIO7 chain uses RMT DMA with 512 symbols (2 KiB). A DMA half-buffer holds 256 symbols, enough for all 216 data bits plus reset and terminator, avoiding Wi-Fi-delayed mid-frame refills that can corrupt colors. Initialization logs the LED count, GPIO, green level, and DMA buffer size. This change targets a confirmed software risk; hardware signal integrity and the actual LED type still require board validation.

板级资源约束：ESP32-S3 仅最后一个 RMT TX 通道支持 DMA。板管理器会在设备 init_skip 判断前初始化全部外设；因此默认 GPIO38/rmt_tx 必须关闭 DMA，并将 mem_block_symbols 设为 48，避免占用底板 GPIO7 所需通道。修改源 board_peripherals.yaml 后必须重新生成板级代码，不能只改源 YAML 或手改生成文件。若启动出现 Key backlight unavailable: ESP_ERR_NOT_FOUND，应优先核对这一分配。

### 6028/3528 WS2812B timing and diagnosis

The board profile explicitly uses the typical XL-3528RGBW-WS2812B 24-bit
GRB timing: T0H/T0L = 250/1000 ns, T1H/T1L = 850/400 ns, with a 400 us
low reset. A 20 MHz RMT resolution represents these times exactly.
This replaces the library default (300/900 ns and 900/600 ns), keeping
the existing DMA allocation and key-to-LED mapping.
Reference: [manufacturer datasheet, page 8](https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/8903/XL-3528RGBW-WS2812B.pdf).
The reported package/model does not identify the manufacturer or silicon
revision, so compatibility must still be confirmed on the actual board.

Startup prints the marker `3528-800k`. Each changed key state prints
`pressed` and `off_leds` masks, with bit 0 corresponding to key/LED 1.
Pressing key 1 alone should print `pressed=0x001 off_leds=0x001`;
releasing all keys should print `0x000` for both. These are the software
scan state and commanded LED state, not a measurement of the wire waveform.
If a single-bit mask accompanies several dark LEDs or non-green colors,
capture that log and the LED positions to investigate the physical data path.
A successful build does not establish that the reported hardware symptom
has been corrected.
