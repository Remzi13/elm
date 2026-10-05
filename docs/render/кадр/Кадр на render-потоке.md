---
tags: [render, frame]
---
# Кадр на render-потоке

Шаги `RenderSystem::RenderNextFrame` (`render/RenderSystem.cpp`):

1. **Ждёт кадр** из [[FrameRing]].
2. **Выполняет команды ресурсов** (`frame.resourceCommands`) в бэкенде ([[RenderResources]]).
3. **Обрабатывает окна:** ресайзит основной swap chain под `backbufferSize` и применяет
   события UI-окон (create, resize, destroy).
4. **Сортирует `drawItems` по мешу**, чтобы получились instanced-группы
   ([[Меши и инстансинг]]).
5. **`RenderPipeline::Render`:**
   - каждая [[Фичи|фича]] в `Setup` добавляет пасы в новый [[RenderGraph]];
   - `Compile()`: отсечение пасов, уровни, барьеры;
   - `Execute()`: запись [[CommandList|command list'ов]], параллельно для пасов одного
     уровня ([[Параллельная запись]]), затем `IRenderBackend::Submit`.
6. **`Present`** сначала для дополнительных окон, потом для основного.
7. **Возвращает слот** в [[FrameRing]] и обновляет статистику ([[Отладка рендера]]).

## См. также
- [[Архитектура рендера]], [[FrameSnapshot]]
