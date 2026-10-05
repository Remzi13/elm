---
tags: [render, rhi, memory]
---
# UploadAllocator

Файл: `render/rhi/UploadAllocator.hpp`. Линейная CPU-память кадра с тремя аренами:

| Арена | Размер | Для чего |
|---|---|---|
| `Vertex` | 32 МБ | инстансы, вершины UI |
| `Index` | 8 МБ | индексы UI |
| `Constants` | 1 МБ | константы шейдеров |

- **Lock-free.** Аллокации можно делать из нескольких воркеров одновременно
  ([[Параллельная запись]]).
- **Одна копия на GPU.** Бэкенд копирует использованную часть каждой арены одним вызовом
  на кадр.
- **Сброс в начале кадра.**

```cpp
auto& uploads = context.Uploads();

// Готовые данные: одна строка
struct Constants { Matrix4x4 viewProjection; Vector4 tint; } c { ... };
rhi::UploadRef cRef = uploads.Upload(rhi::UploadArena::Constants, &c, 1);
cmd.SetConstants(0, cRef);

// Заполнение на месте
auto alloc = uploads.Allocate(rhi::UploadArena::Vertex, sizeof(InstanceData) * count);
if (!alloc.IsValid())
    return;                                  // арена переполнена: пропустить, не падать
auto* out = static_cast<InstanceData*>(alloc.data);
for (...) *out++ = ...;
cmd.BindVertexBuffer(1, rhi::BufferSource::FromUpload(alloc.ref));
```

`UploadRef` годится только для текущего кадра. Для постоянных данных используйте
[[Буферы]].

## См. также
- [[CommandList]], [[Ограничения рендера]]
