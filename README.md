# ESP32-P4 智能显示终端 (Phone-style UI + YOLO Pose)

基于 **ESP32-P4** 的智能显示终端，运行类手机 UI（状态栏、桌面、通知面板、App 管理器），集成 **OV5647 摄像头** 实时预览与 **YOLO11n-pose 人体姿态估计**，通过 **ESP32-C6 协处理器** 提供 WiFi 联网能力。

---

## 硬件平台

| 组件 | 型号 | 说明 |
|------|------|------|
| **SoC** | ESP32-P4 (RISC-V 双核) | 主频 360MHz（芯片批次限制），VPU 向量处理单元 |
| **PSRAM** | 32MB Octal SPI | 200MHz，主堆区 (`CONFIG_SPIRAM_USE_MALLOC=y`) |
| **内部 SRAM** | 428KB（碎片化） | 最大连续可用块 ~31KB，不足以容纳 YOLO 激活张量 |
| **L2 Cache** | 256KB | 512KB 在本芯片版本上导致启动崩溃 |
| **显示** | 7" MIPI DSI 1024×600 (EK79007) | LVGL v9.5，PPA 硬件加速渲染 |
| **摄像头** | OV5647 MIPI CSI | ISP 输出 RGB565 800×800 |
| **WiFi** | ESP32-C6 (SDIO) | 通过 `esp_wifi_remote` + `esp_hosted` 驱动 |
| **SD 卡** | SDMMC Slot 0 | 存储模型文件 (`coco_pose_yolo11n_pose_s8_v1.espdl`)、字体、WiFi 配置 |

---

## 软件架构

```
┌──────────────────────────────────────────────────────────────┐
│                        app_main()                            │
│     ServiceManager: 按依赖顺序启动 / 主循环 dispatch          │
├──────────┬──────────┬──────────┬──────────┬──────────────────┤
│ Display  │ SdCard   │  WiFi   │ Camera   │     YOLO         │
│ Service  │ Service  │ Service │ Service  │   Service        │
│ (LVGL    │ (SD卡    │ (C6协   │ (OV5647  │ (ESP-DL          │
│  +PPA)   │  挂载)   │  处理器)│  ISP)    │  PPA+VPU)        │
├──────────┴──────────┴──────────┴──────────┴──────────────────┤
│                        EventBus                               │
│           (发布/订阅，支持同步 post 与异步 postAsync)          │
├───────────────────────────────────────────────────────────────┤
│  LUF — LVGL User-interface Framework                          │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌────────────────┐   │
│  │  状态栏   │ │  桌面页   │ │ 通知面板  │ │  App 管理器    │   │
│  │ 常驻顶部   │ │ TileView  │ │ 下拉通知  │ │  打开/关闭/   │   │
│  │ 左右两簇   │ │ 多页横划  │ │ 支持消息  │ │  手势返回     │   │
│  └──────────┘ └──────────┘ └──────────┘ └────────────────┘   │
└───────────────────────────────────────────────────────────────┘
```

### 设计模式：Service + EventBus

所有子系统由 `Service` 基类统一生命周期管理：

```
构造函数 → init() → (运行中) → deinit() → 析构
```

- **`Service`** — 抽象基类，提供 `name()`、`bus()` 访问 EventBus
- **`ServiceManager`** — 模板工厂，按注册顺序 `start()` 依次 init，逆序 deinit
- **`EventBus`** — 发布/订阅事件总线，支持同步 `post()` 和跨任务异步 `postAsync()` + `dispatch()`

### 内置事件

| 事件 | 触发时机 |
|------|----------|
| `EV_SYS_STARTUP` | 所有服务 init 完成后 |
| `EV_DISPLAY_READY` | 显示屏 + LVGL 初始化完毕 |
| `EV_SDCARD_MOUNTED` | SD 卡挂载成功 |
| `EV_SDCARD_ERROR` | SD 卡挂载失败 |
| `EV_WIFI_CONNECTED` | WiFi 连接成功 |
| `EV_WIFI_DISCONNECTED` | WiFi 断开 |
| `EV_CAMERA_READY` | 摄像头初始化完成 |
| `EV_CAMERA_FRAME` | 新摄像头帧就绪 |
| `EV_CAMERA_ERROR` | 摄像头错误 |
| `EV_TIME_SYNC` | NTP 时间同步完成 |

---

## 服务详解

### 1. DisplayService — 显示 + LVGL

- 初始化 BSP 显示屏（MIPI DSI 1024×600）
- 启动 LVGL，配置 PPA 硬件加速渲染（`lv_draw_ppa_init()`）
- LVGL 绘制线程栈: **16KB**（PPA 渲染需要更大栈空间）
- 初始化 LUF 框架 + demo 内容
- 创建 10ms 定时器排空 `luf_pump()` 消息队列

### 2. SdCardService — SD 卡

- SDMMC Slot 0，与 ESP32-C6（Slot 1 SDIO）共享 SDMMC 控制器
- 挂载成功后发布 `EV_SDCARD_MOUNTED`
- YOLO 模型从此服务挂载的 SD 卡读取

### 3. WifiService — WiFi

- 通过 `esp_wifi_remote` + `esp_hosted` 驱动 ESP32-C6 协处理器
- 支持从 SD 卡加载保存的 WiFi 配置
- 发布 `EV_WIFI_CONNECTED` / `EV_WIFI_DISCONNECTED`

### 4. CameraService — 摄像头

- 基于 V4L2 + ISP 的 OV5647 驱动
- RGB565 帧输出（通过 `app_video.c` 的 V4L2 接口）
- 帧回调通知 `EV_CAMERA_FRAME`
- 支持 start/stop 流控制（Camera App 打开/关闭时）

### 5. YoloService — YOLO 推理

- 模型：YOLO11n-pose (int8 量化)，640×640 输入，COCO 17 关键点
- 从 SD 卡加载模型（`fbs::MODEL_LOCATION_IN_SDCARD`）
- 推理运行在独立 FreeRTOS 任务（Core 1，优先级 2）
- 通过 C API（`yolo_c_api.h`）供 LVGL/C 代码调用

#### 推理流水线

```
Camera RGB565 800×800
       │
       ▼
PPA SRM (硬件缩放 + 色彩转换)
  RGB565 → BGR888, 800→640
       │
       ▼
ImagePreprocessor (归一化 + Letterbox)
  mean=0, std=255, rgb_swap=true
       │
       ▼
Model::run() (ESP-DL + VPU 向量指令)
  640×640 int8 → 17 关键点 + 边框
       │
       ▼
yolo11posePostProcessor (NMS + 关键点解码)
       │
       ▼
坐标缩放回原始分辨率 → 骨骼叠加渲染到 RGB565 帧
```

#### 性能数据

| 阶段 | 耗时 | 说明 |
|------|------|------|
| PPA 硬件预处理 | ~5ms | RGB565→BGR888 + 缩放，原软件 ~78ms |
| 模型推理 | ~7937ms | **主要瓶颈**，全部激活张量在 PSRAM |
| 后处理 | ~1ms | NMS + 关键点解码 |
| **总计** | **~8s** | 受限于 PSRAM 延迟 + 360MHz CPU |

#### 优化历程

| 尝试 | 结果 |
|------|------|
| ✅ **PPA 硬件预处理** | 78ms → 5ms，大幅降低预处理延迟 |
| ✅ **`minimize()` 释放元数据** | 减少 PSRAM 占用 |
| ✅ **VPU (PIE V2)** | `esp.vld.128` / `esp.vst.128` / `esp.vrelu.s8` 指令已确认激活 |
| ❌ **CPU 400MHz** | 芯片批次限制，最高 360MHz |
| ❌ **L2 Cache 512KB** | 启动崩溃（`reclaim_startup_stack_memory_for_heap`） |
| ❌ **max_internal_size** | 内部 SRAM 最大连续块仅 31KB |
| ✅ **P4 专用模型 v1** | 已切换到 `coco_pose_yolo11n_pose_s8_v1.espdl`（待测试） |

---

## LUF 框架 (LVGL User-interface Framework)

仿手机界面的 LVGL UI 框架，纯 C 实现：

### 状态栏 (`luf_statusbar`)
- 常驻 `lv_layer_top()`，不随 screen 切换重建
- 左簇 / 右簇两个 flex 容器，支持任意内容项
- 项句柄使用不透明代数 id，删除后旧 id 自动失效

### 桌面 (`luf_desktop`)
- `lv_tileview` 实现多页横划切换
- 按需填充（fill/clear 回调），仅当前页 ±1 有内容
- 吸附动画由 tileview 原生处理

### 通知面板 (`luf_notify`)
- 下拉面板，初始隐藏在屏幕上方外
- 手势驱动跟手下拉与回弹
- 支持通用通知项 + 便捷消息卡片

### App 管理器 (`luf_app_mgr`)
- App 注册表（名称、图标、构建函数、全屏标志）
- 打开时全屏覆盖 + 返回手势（边缘右滑）
- LVGL group 焦点管理

### 手势状态机 (`luf_gesture`)
- 处理：下拉通知 / 边缘返回 / 横划切页
- 多指互斥逻辑

---

## 项目结构

```
hello_world/
├── CMakeLists.txt              # 顶层 CMake（idf 项目）
├── partitions.csv              # 分区表（双 OTA，各 5MB）
├── sdkconfig                   # 当前配置
├── sdkconfig.defaults          # 默认 Kconfig 覆写
├── dependencies.lock           # 组件依赖锁定
├── main/
│   ├── CMakeLists.txt          # 主组件构建
│   ├── idf_component.yml       # 组件清单
│   ├── Kconfig.projbuild       # 项目 Kconfig（WiFi SSID 等）
│   ├── main.cpp                # 入口：ServiceManager 组装
│   ├── camera.c / .h           # OV5647 摄像头驱动
│   ├── sd_card.c / .h          # SD 卡挂载
│   ├── wifi.c / .h             # WiFi 驱动
│   ├── app_video.c / .h        # V4L2 视频接口
│   ├── core/
│   │   ├── service.h           # Service 基类
│   │   ├── service_manager.h   # ServiceManager 模板
│   │   └── event_bus.h / .cpp  # EventBus 事件总线
│   ├── services/
│   │   ├── display_service.h / .cpp   # 显示 + LVGL
│   │   ├── sdcard_service.h / .cpp    # SD 卡服务
│   │   ├── wifi_service.h / .cpp      # WiFi 服务
│   │   ├── camera_service.h / .cpp    # 摄像头服务
│   │   ├── yolo_service.h / .cpp      # YOLO 推理服务
│   │   ├── yolo_c_api.h              # YOLO C 接口
│   │   └── system/
│   │       └── time_service.h / .cpp  # NTP 时间同步
│   ├── luf/                    # 手机风格 UI 框架
│   │   ├── luf.c / .h         # 框架总装
│   │   ├── luf_osal.c / .h    # OS 抽象层
│   │   ├── core/
│   │   │   ├── luf_app.h / .c         # App 注册表
│   │   │   ├── luf_app_mgr.h / .c     # App 管理器
│   │   │   ├── luf_desktop.h / .c     # 桌面（tileview）
│   │   │   ├── luf_gesture.h / .c     # 手势状态机
│   │   │   ├── luf_notify.h / .c      # 通知面板
│   │   │   └── luf_statusbar.h / .c   # 状态栏
│   │   └── port/
│   │       └── luf_osal_freertos.c    # FreeRTOS 适配
│   ├── demo/                   # 示例内容
│   │   ├── demo.h / .c         # 一键装配
│   │   ├── demo_apps.c         # App 示例注册
│   │   ├── demo_pages.c        # 桌面页示例
│   │   ├── demo_flipclock.c    # 翻页时钟 App
│   │   ├── demo_wifi.c         # WiFi 设置 App
│   │   └── ...
│   └── models/
│       └── coco_pose_yolo11n_pose_s8_v1.espdl  # P4 优化姿态模型
├── managed_components/         # IDF 组件管理器下载
└── build/                      # 构建产物
```

---

## 依赖组件

| 组件 | 版本 | 用途 |
|------|------|------|
| `lvgl/lvgl` | ^9.2.0 | GUI 框架 |
| `waveshare/esp32_p4_wifi6_touch_lcd_7b` | * | BSP 板级支持包 |
| `espressif/esp_hosted` | ^2.12.9 | C6 SDIO 宿主驱动 |
| `espressif/esp_wifi_remote` | ^1.6.1 | C6 远程 WiFi |
| `espressif/esp_video` | ^2.3.0 | V4L2 视频框架 |
| `espressif/esp-dl` | ^3.0.0 | 深度学习推理框架 |
| `espressif/esp_driver_ppa` | — | PPA 像素处理加速器 |
| `espressif/esp_new_jpeg` | ^1 | JPEG 编解码 |

---

## 关键配置 (sdkconfig)

| 配置 | 值 | 说明 |
|------|-----|------|
| `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ` | 360 | CPU 主频 |
| `CONFIG_SPIRAM_USE_MALLOC` | y | PSRAM 作为主堆 |
| `CONFIG_SPIRAM_SPEED` | 200MHz | PSRAM 频率 |
| `CONFIG_CACHE_L2_CACHE_256KB` | y | L2 缓存大小 |
| `CONFIG_LV_USE_PPA` | y | LVGL PPA 加速 |
| `CONFIG_LV_USE_PPA_IMG` | y | LVGL PPA 图像加速 |
| `CONFIG_LVGL_PORT_ENABLE_PPA` | y | LVGL 端口 PPA 启用 |
| `CONFIG_LV_DRAW_BUF_ALIGN` | 128 | LVGL 绘制缓冲对齐 |
| `CONFIG_LV_DRAW_THREAD_STACK_SIZE` | 16384 | LVGL 绘制线程栈 |
| `CONFIG_DMA2D_ISR_IRAM_SAFE` | n | 禁用（PPA 回调在 Flash） |
| `CONFIG_DMA2D_OPERATION_FUNC_IN_IRAM` | y | DMA2D 操作函数在 IRAM |
| `CONFIG_PIE_V2_BOOST` | y | VPU 向量加速（ESP-DL 硬编码） |

---

## 构建与烧录

```bash
# 设置目标芯片
idf.py set-target esp32p4

# 构建
idf.py build

# 烧录
idf.py flash

# 监控输出
idf.py monitor
```

### SD 卡内容

```
/sdcard/
├── models/
│   └── coco_pose_yolo11n_pose_s8_v1.espdl  # P4 优化
├── fonts/         (可选：中文字体)
└── wifi.conf      (可选：WiFi 配置)
```

---

## App 开发指南

在 `demo/demo_apps.c` 中用 `luf_app_register()` 注册新 App：

```c
static void my_app_build(lv_obj_t *content)
{
    lv_obj_t *label = lv_label_create(content);
    lv_label_set_text(label, "Hello from MyApp!");
    lv_obj_center(label);
}

// 在 demo_apps() 中注册：
luf_app_t my_app = luf_app_register(&(struct luf_app_desc){
    .name = "我的应用",
    .symbol = LV_SYMBOL_SETTINGS,
    .color = 0x2196F3,
    .build = my_app_build,
    .fullscreen = false,
});
```

---

## 限制与已知问题

1. **YOLO 推理速度 ~8 秒** — PSRAM 延迟是根本瓶颈，内部 SRAM 碎片化无法容纳激活张量
2. **CPU 最高 360MHz** — 该芯片批次不支持 400MHz
3. **L2 缓存最高 256KB** — 512KB 在本芯片版本上 boot panic
4. **PPA 要求** — DMA2D ISR IRAM safe 必须关闭，因为 PPA 回调在 Flash 中
5. **双 OTA 分区** — 各 5MB，固件大小需注意
