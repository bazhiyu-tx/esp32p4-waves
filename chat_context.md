# ESP32-P4 + LVGL 移植对话记录

## 项目路径
`C:\Users\OPM_099\Desktop\hard\ESP32P4\hello_world`

## 已完成的工作

### 1. SDK 配置（menuconfig）
| 配置项 | 设置 |
|--------|------|
| 目标芯片 | ESP32-P4 Rev v1.0 |
| Flash 模式 | QIO, 80MHz, 32MB |
| 编译器优化 | Performance (-O2) |
| PSRAM | 16-Line-Mode, 200MHz, XiP from PSRAM, malloc 可用 |
| Cache | L2 256KB, Line 128B |
| FreeRTOS | Tick 1000Hz, 栈 10240, 显示 CoreID, 运行统计 |
| DMA2D | IRAM + ISR Safe |
| GDMA | IRAM + ISR Safe |
| 分区表 | Two OTA partitions (large) |
| 任务 WDT | 超时 5s（建议关掉或改大） |

### 2. LVGL 配置（待确认字体已勾选）
需在 menuconfig 中勾选字体：
- Montserrat 12, 16, 18, 20, 24, 28, 48
- 色彩深度：32: XRGB8888
- Malloc：Standard C functions
- OS：FreeRTOS
- 绘制单元数：2
- 刷新率：15ms

### 3. LVGL + 板级支持包依赖
`main/idf_component.yml`:
```yaml
dependencies:
  lvgl/lvgl: "^9.2.0"
  waveshare/esp32_p4_wifi6_touch_lcd_7b: "*"
```

### 4. UI 框架移植
从 `C:\Users\OPM_099\Desktop\hard\ui (1)\lvgl_sim` 移植了 LUF (Light UI Framework)：
- `main/luf/` - 框架核心（桌面、状态栏、通知、App管理器、手势）
- `main/demo/` - 示例 App（翻牌时钟、设置、音乐等）

### 5. 已知问题
1. 任务 WDT 触发器 - main 任务被 LVGL 事件阻塞，已改为 vTaskDelete(NULL)
2. 格式说明符 - RISC-V 上 uint32_t 需用 %lu 而非 %u（已修复多处）
3. 翻牌时钟动画 - 需要 lv_timer 定时调用 luf_pump()（已修复）

## 移植步骤总结
1. `idf.py reconfigure` - 下载组件
2. `idf.py menuconfig` - 配置 PSRAM/Cache/LVGL/分区表
3. `idf.py build` - 编译
4. `idf.py -p PORT flash` - 烧录
5. `idf.py -p PORT monitor` - 查看输出
