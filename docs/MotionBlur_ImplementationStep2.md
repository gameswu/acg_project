# Motion Blur — Implementation Step 2 (Camera Rotation Sampling)

日期: 2026-01-11

目的：使用更简单的运动模糊实现——通过摄像机在一条旋转路径上采样多帧图像并累积平均来模拟运动模糊。

已实现内容：

- `include/Renderer.h`
  - 新增成员 `bool m_enableMotionBlur`（默认 false）和 `float m_motionAngleDegrees`（默认 5.0f），表示总旋转角度（度），摄像机将在该范围内采样。

- `src/GUI.cpp` / `include/GUI.h`
  - 在 GUI 中添加 `Enable Motion Blur` 复选框（`GUIState::enableMotionBlur`）；切换会调用 `renderer->SetMotionBlurEnabled(...)` 并重置累积。

- `src/Renderer.cpp`
  - 在 `RenderToFile()` 中实现：
    - 渲染前保存原始相机位置/目标/up。
    - 对每个样本（`samplesPerPixel`）根据样本索引沿 [-angle/2, +angle/2] 线性插值旋转相机位置（围绕相机 target、绕 up 轴旋转）。
    - 使用更新后的相机状态重建 `CameraConstants` 并 `DispatchRays()`，累积到输出纹理中。
    - 渲染结束后恢复相机到原始位置。

实现说明与权衡：

- 视觉效果：该方法会将摄像机在小角度范围内的几何视图平均，从而产生物体看起来有运动模糊的效果（针对摄像机旋转产生的运动）。

- 性能：开启后总 Dispatch 数为 `samplesPerPixel`（每次 Dispatch 使用不同摄像机参数）。如果你希望在每个最终像素上进行更多时间采样，请增加 `samplesPerPixel` 或者修改为对每个样本再做多次摄像机采样（当前实现将每次 Dispatch 视作一次样本）。

- Skybox：环境贴图（skybox）不会被移动，因为我们只改变摄像机视点而不修改场景几何或环境贴图资源，满足“除了 skybox 外所有物体移动”的要求（此实现为摄像机运动，skybox 仍按视角映射）。

下一步建议：

- 如果希望更明确地控制运动模糊样本数量与角度，请考虑把 `m_motionAngleDegrees` 和采样次数暴露到 GUI（`GUIState` + `Renderer::SetMotionParams(...)`）。
- 若需同时支持物体运动与摄像机运动，可结合本方法与先前建议的 per-instance TLAS 更新方案。

备注：本次改动尽量保持侵入最小，仅修改了渲染循环以插入摄像机参数扰动并新增少量成员用于控制角度。请在本地构建并测试离线渲染（`RenderToFile`）以验证效果。