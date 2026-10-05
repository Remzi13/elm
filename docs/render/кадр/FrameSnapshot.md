---
tags: [render, frame]
---
# FrameSnapshot

Файл: `render/frame/FrameSnapshot.hpp`. Здесь лежит всё, что нужно render-потоку для одного
кадра. Update заполняет снапшот через [[FrameWriter]]. Затем снапшот принадлежит
render-потоку, пока кадр не нарисован ([[FrameRing]]).

| Поле | Что |
|---|---|
| `frameIndex` | номер кадра |
| `backbufferSize` | размер основного окна |
| `sceneView` | `CameraData` + текстура 3D-вида + размер ([[ViewPort]]) |
| `drawItems` | `DrawItem { MeshHandle mesh; InstanceData instance; }` |
| `overlay` | `OverlayFrame`: геометрия ImGui по окнам и события окон |
| `resourceCommands` | команды [[RenderResources]] для этого кадра |

`Reset()` очищает снапшот, сохраняя выделенную память. Поэтому в установившемся режиме кадры
не выделяют память.

В execute-лямбде паса снапшот доступен только на чтение через `context.Frame()`
([[RenderGraph]]).

## Свои данные для render-потока

Например, нужно передать список выделенных объектов. Это три шага.

1. Добавить поле в `FrameSnapshot` и очистить его в `Reset()`:

   ```cpp
   struct FrameSnapshot {
       ...
       Vector<DrawItem> highlightItems;

       void Reset() { ...; highlightItems.clear(); }
   };
   ```

2. Добавить запись в [[FrameWriter]]:

   ```cpp
   void Highlight(MeshHandle mesh, const Matrix4x4& transform, const Vector4& color)
   {
       if (m_frame && mesh.IsValid())
           m_frame->highlightItems.push_back({ mesh, { transform, color } });
   }
   ```

3. Читать поле в пасе через `context.Frame().highlightItems`. Полный пример:
   [[Пример HighlightPass]].

> [!warning] Только копии
> В снапшот кладутся **копии**, а не указатели на объекты update-потока. Update может
> изменить или удалить объект, пока render ещё рисует кадр.

## См. также
- [[Кадр на render-потоке]]
