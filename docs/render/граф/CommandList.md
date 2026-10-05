---
tags: [render, graph, rhi, api]
---
# CommandList

Файл: `render/rhi/CommandList.hpp`. Это список POD-команд, не зависящий от бэкенда. Каждый пас
записывает свой список, возможно на воркере ([[Параллельная запись]]). Бэкенд транслирует
списки по порядку на render-потоке.

> [!important] Чистое состояние
> Каждый список транслируется с чистого состояния. Внутри списка нужно начать render pass,
> поставить пайплайн, подключить ресурсы и только потом рисовать.

```cpp
auto& cmd = context.Commands();
cmd.BeginRenderPass(color, depth);                   // таргеты и viewport на весь таргет
cmd.SetPipeline(pipeline);
cmd.SetViewport(0, 0, 640, 360);                     // по желанию
cmd.SetScissor(left, top, right, bottom);            // если в пайплайне scissor = true
cmd.SetConstants(/*slot*/ 0, constantsRef);          // binding типа Constants
cmd.BindTexture(/*slot*/ 1, texture, fallback);      // binding типа Texture
cmd.BindVertexBuffer(0, rhi::BufferSource::Vertices(mesh));
cmd.BindVertexBuffer(1, rhi::BufferSource::FromUpload(instancesRef));
cmd.BindIndexBuffer(rhi::BufferSource::Indices(mesh));
cmd.DrawIndexed(/*indexCount, 0 = весь меш*/ 0, /*instances*/ count);
cmd.EndRenderPass();
```

Откуда брать attachment'ы `color`/`depth`, описано в [[Ресурсы графа#В execute]]. Как
получить `constantsRef` и `instancesRef`, описано в [[UploadAllocator]]. Слоты задаются
в [[Шейдеры и пайплайны]].

## Источники вершин и индексов

| `rhi::BufferSource::` | Что |
|---|---|
| `Vertices(mesh)` / `Indices(mesh)` | буферы меша ([[Меши и инстансинг]]) |
| `FromBuffer(buffer, offset)` | `BufferHandle` ([[Буферы]]) |
| `FromUpload(ref)` | данные этого кадра ([[UploadAllocator]]) |

Индексы всегда 32-битные. Слот `SetConstants`/`BindTexture` — это индекс элемента в
`PipelineDesc::bindings`.

## Барьеры

`Barrier(texture, state)` есть, но обычно не нужен: [[RenderGraph]] ставит барьеры сам по
объявлениям `Read`/`Write*`.

## См. также
- [[Пример HighlightPass]]
