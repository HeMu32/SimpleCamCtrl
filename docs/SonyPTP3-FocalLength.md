# SonyPTP3 焦距读取修复记录

## 背景

`demo_driver` 在 ILCE-7M4 上输出 `FocalLength(mm): 0.0000`，无法反映镜头当前焦距。

本次目标是让 `ISimpleCamCtrl::ExposureParams::focal_length` 返回真实焦距（例如 34/35/36mm 附近），并随变焦变化。

## 问题现象与定位结论

在相机连接正常、快门/光圈/ISO 可读的前提下，焦距相关属性表现如下：

- `0x5008 (PTP Focal Length)`：未返回。
- `0xD193 (Image Stabilization Steady Shot Focal Length)`：存在但常为 `0`，且在该机型上不可直接用于当前焦距。
- `0xD2D6 (vendor fallback)`：未返回。
- `0xD1C0`：值稳定在 `100`，不随变焦变化，实测不是当前焦距。
- `0xD00B (Zoom Distance)`：返回有效值，例如 `34000`，按文档单位换算后为 `34.000 mm`，与实际镜头焦段一致。

结论：在 ILCE-7M4 + WIA/PTP3 场景下，真实可用的当前焦距来源是 `0xD00B`，不是 `0xD1C0`。

## 修复原理

### 1. 先确保拿到完整属性集

`SDIO_GetAllExtDeviceInfo (0x9209)` 改为优先使用扩展参数：

- 首选：`{0, 1}`（全量 + extended option）
- 回退：`{0}`
- 再回退：`{}`

这样可以兼容不同机型/固件返回策略，同时尽可能获取完整属性。

### 2. 焦距属性优先级与换算

在 `UpdateStatus()` 中按以下优先级计算 `focal_length`：

1. `0x5008`：`value / 100.0`（0.01mm）
2. `0xD00B`：`value / 1000.0`（0.001mm，Zoom Distance）
3. `0xD193`：直接按 mm 使用（仅 `> 0` 时）
4. `0xD2D6`：`value / 100.0`（保留历史 vendor 兼容）但是也可能有兼容问题

同时移除 `0xD1C0` 作为焦距来源，避免输出固定错误值（100mm）。

## 代码落点

- 常量定义：
  - `src/SonyPTP3/SonyPTP3_aux.h`
  - 新增 `DPC_ZOOM_DISTANCE = 0xD00B`
- 读取与计算逻辑：
  - `src/SonyPTP3/SonyPTP3_Impl.cpp`
  - `UpdateStatus()` 内 `0x9209` 参数策略与焦距优先级逻辑

## 验证结果

本地实机复测（ILCE-7M4）：

- 修复前：`FocalLength(mm): 0.0000`
- 误判阶段（使用 `D1C0`）：`FocalLength(mm): 100.0000`（不随变焦变化）
- 修复后（使用 `D00B`）：例如 `FocalLength(mm): 34.0000`，与实际焦段一致

## 后续建议

- 若后续发现其他机型 `D00B` 缺失或单位异常，建议保留当前优先级并按机型增加白名单/特判。
- 若要进一步提高稳定性，可在调试模式下打印“焦距来源属性码”，便于快速排查机型差异。
