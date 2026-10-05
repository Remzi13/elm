---
tags: [render, frame, ui, howto]
---
# ViewPort

Файл: `render/ViewPort.hpp`. Это update-сторона 3D-вида. Владеет цветовой текстурой
(`RGBA8_UNORM_SRGB`, RenderTarget + ShaderResource), в которую `ScenePass` рисует сцену, а
UI её показывает.

```cpp
render::ViewPort viewPort(renderSystem.Resources(), { 1280, 720 });

// каждый кадр, например из окна ImGui
viewPort.SetSize({ uint32_t(available.x * dpi), uint32_t(available.y * dpi) });
ImGui::Image(ImGuiSystem::ToTextureId(viewPort.GetColorTexture()), available);

// и передать в кадр
frame.SetSceneView(camera, viewPort.GetColorTexture(), viewPort.GetSize());
```

`SetSize` при изменении размера отправляет `ResizeTexture` через очередь команд
([[Текстуры#Ресайз]]). Render-поток сам текстуру никогда не пересоздаёт. Глубина 3D-вида
живёт в графе как transient-ресурс ([[Ресурсы графа]]).

## Любая текстура в ImGui

```cpp
ImTextureID id = ImGuiSystem::ToTextureId(texture.GetHandle());   // хэндл упакован в младший бит
ImGui::Image(id, ImVec2(256, 256));
```

`OverlayPass` распаковывает хэндл и подключает текстуру. Если текстуры ещё или уже нет,
рисуется белая fallback-текстура ([[Время жизни ресурсов#Устаревший хэндл]]).

## См. также
- [[FrameWriter]], [[Фичи]] (`ScenePass`, `OverlayPass`)
