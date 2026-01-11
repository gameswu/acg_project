# Motion Blur — Implementation Step 1

日期: 2026-01-11

目的：记录为实现动态（运动）模糊所做的第一批小改动（低侵入、先搭接口），并说明下一步工作。

已修改内容：

- `include/Renderer.h`
  - 新增成员与接口（第一阶段）：
    - `bool m_enableMotionBlur`（默认 `false`）
    - `float m_motionSpeed`（固定速度，默认 `0.5f` world units/sec）
    - 内联方法 `SetMotionBlurEnabled(bool)` 与 `IsMotionBlurEnabled()`，用于快速打开/关闭运动模糊功能（暂不修改渲染流程）

- `include/GUI.h`
  - 在 `GUIState` 中新增 `bool enableMotionBlur` 用于保存 GUI 开关状态。

- `src/GUI.cpp`
  - 在摄像机/DOF 区块添加了一个复选框 `Enable Motion Blur`：
    - 勾选时会调用 `renderer->SetMotionBlurEnabled(state.enableMotionBlur)` 并重置累积（`ResetAccumulation()`）。
    - 速度参数当前写死在 `Renderer`（`m_motionSpeed`），GUI 暂不暴露该数值以满足“速度写死在项目里”的要求。

说明与下一步计划：

- 本次改动仅建立了最小化的 UI 与 API 接口，尚未实现实际的运动模糊渲染逻辑（即没有修改加速结构或 shader 来进行时域采样）。

- 推荐的下一步（优先级）:
  1. 重构加速结构构建逻辑：将目前的“单一 BLAS + 单实例 TLAS”改为“每个 mesh 单独 BLAS + 在 TLAS 中为每个 mesh 创建实例”，以便可以仅重建 TLAS 来反映物体位置随时间的变化（不必每次重建昂贵的 BLAS）。
  2. 在渲染循环中引入快门时间采样策略（每个 sample/每个 batch 在 [0, shutter] 范围内采样时间），针对采样时间计算每个实例的变换并更新 `m_instanceDescBuffer`，然后仅重建 TLAS 并 DispatchRays。
  3. （可选）在 `CameraConstants` 中添加时间字段（例如 `float sampleTime`），并在 shader 中读取用于基于时间的材质/程序化运动等扩展。

- 风险/注意事项：
  - 如果场景非常大或所有几何严格合并为单 BLAS，短期内对性能影响会很大（若按样本重建 BLAS）。因此必须优先做 per-instance TLAS 改动。
  - TLAS 重建仍会增加开销，建议按 `m_renderBatchSize` 做权衡（每 batch 共享一个 TLAS，或每样本重建以提高质量）。

如果你同意，我会继续：
- 实现第 1 步（重构加速结构以支持 per-mesh BLAS + per-instance TLAS），并在完成后推送下一版文档与代码变更清单。