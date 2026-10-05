---
tags: [render, frame, api]
---
# FrameWriter

Файл: `render/api/FrameWriter.hpp`. Это writer кадра на update-стороне. Его выдаёт
`RenderSystem::BeginFrame()`, а возвращают вызовом `RenderSystem::SubmitFrame()`. Всё, что
записано через него, **копируется** в [[FrameSnapshot]], поэтому после `SubmitFrame` сцену
можно сразу менять.

```cpp
auto frame = renderSystem.BeginFrame();   // может подождать, если render отстал
if (!frame)
    return;                               // рендер остановлен

// 1. Размер основного окна в пикселях
frame.SetBackbufferSize(window.GetFramebufferSize());

// 2. 3D-вид: камера и целевая текстура, которую показывает UI
const render::CameraData camera { camera.GetViewProjectionMatrix(), camera.GetPosition() };
frame.SetSceneView(camera, viewPort.GetColorTexture(), viewPort.GetSize());

// 3. Объекты
frame.ReserveDraws(scene.instances.size());
for (const auto& inst : scene.instances)
    if (inst.visible)
        frame.Draw(inst.renderMesh, inst.worldTransform, inst.color);

// 4. UI: заполняет ImGuiSystem::BuildFrame
imgui.BuildFrame(..., frame.Overlay());

renderSystem.SubmitFrame(frame);          // вместе с командами ресурсов
```

## Методы

| Метод | Куда пишет |
|---|---|
| `SetBackbufferSize(size)` | `FrameSnapshot::backbufferSize` |
| `SetSceneView(camera, colorTarget, size)` | `FrameSnapshot::sceneView` |
| `Draw(mesh, transform, color)` | `FrameSnapshot::drawItems` |
| `ReserveDraws(n)` | резервирует место |
| `Overlay()` | `FrameSnapshot::overlay` (геометрия ImGui) |

Ссылки по пунктам:
- блокировка `BeginFrame` — [[FrameRing]];
- откуда берётся `colorTarget` — [[ViewPort]];
- почему важен один меш на много объектов — [[Меши и инстансинг]].

Нужны свои данные в кадре? См. [[FrameSnapshot#Свои данные для render-потока]].

## См. также
- [[Главный цикл]], [[Кадр на render-потоке]]
