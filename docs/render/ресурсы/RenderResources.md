---
tags: [render, resources, api]
---
# RenderResources

Файл: `render/api/RenderResources.hpp`. Это API ресурсов для update-стороны. **Все методы
можно вызывать из любого потока.**

```cpp
render::RenderResources& resources = renderSystem.Resources();
```

## Как работает

1. `Create*()` сразу выделяет [[Хэндлы|хэндл]] и записывает команду.
2. `Update*()`, `Resize*()` и `Destroy*()` тоже только записывают команды.
3. `RenderSystem::SubmitFrame` забирает все накопленные команды в [[FrameSnapshot]].
4. Render-поток выполняет команды до отрисовки этого кадра ([[Кадр на render-потоке]]).

Команды сохраняют порядок, в том числе между потоками: важен порядок захвата внутреннего
мьютекса. Команды не теряются.

## API

| Метод | Описание |
|---|---|
| `CreateTexture(desc)` / `ResizeTexture` / `UpdateTexture` / `DestroyTexture` | [[Текстуры]] |
| `CreateMesh(data)` / `DestroyMesh` | [[Меши и инстансинг]] |
| `CreateBuffer(desc, data)` / `UpdateBuffer` / `DestroyBuffer` | [[Буферы]] |
| `IsAlive(handle)` | хэндл ещё не удалён на update-стороне |

## RAII-обёртки

`render::Texture` и `render::Mesh` — move-only владельцы. Деструктор сам отправляет
`Destroy*`.

```cpp
render::Texture texture(resources, desc);
render::Mesh mesh(resources, MakeShared<const MeshData>(data));
```

## Типы команд

Все команды перечислены в `render/api/ResourceCommands.hpp`: `CreateTexture`,
`ResizeTexture`, `UploadTexture`, `DestroyTexture`, `CreateBuffer`, `UploadBuffer`,
`DestroyBuffer`, `CreateMesh`, `DestroyMesh`. Их исполняет бэкенд
(`IRenderBackend::ExecuteResourceCommands`, см. [[Новый бэкенд]]).

## См. также
- [[Загрузка ресурсов в фоне]]
- [[Время жизни ресурсов]]
