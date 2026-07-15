#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// 触发 OTA 检查更新（从 GitHub 拉取）
void ota_check_update(void);

#ifdef __cplusplus
}
#endif