---
tags: [render, graph, api]
aliases: [Render graph, PassBuilder, PassContext]
---
# RenderGraph

Файл: `render/graph/RenderGraph.hpp`. Граф строится заново каждый кадр из пасов, которые
объявили [[Фичи]].

## Пас: setup и execute

```cpp
struct Data {
    RGTexture color;
    RGTexture depth;
};

graph.AddPass<Data>("MyPass",
    // setup: выполняется сразу внутри AddPass и объявляет ресурсы
    [&](PassBuilder& builder, Data& data) {
        data.color = builder.WriteColor(target);
        data.depth = builder.WriteDepth(builder.CreateTexture("MyDepth", depthDesc));
        builder.Read(blackboard.sceneColor);
    },
    // execute: выполняется позже, возможно на воркере
    [pipeline = m_pipeline](const Data& data, PassContext& context) {
        auto& cmd = context.Commands();
        ...
    });
```

> [!danger] Захват в execute
> Execute-лямбда вызывается **после** выхода из `Setup`. Захватывать в неё по ссылке
> локальные переменные `Setup` нельзя. Захватывайте по значению: хэндлы, параметры. Всё,
> что нужно из кадра, берите из `context.Frame()`. Setup-лямбду можно захватывать по ссылке
> (`[&]`), она выполняется синхронно.

## PassBuilder (setup)

| Метод | Что означает | Переход состояния |
|---|---|---|
| `CreateTexture(name, desc)` | transient-текстура графа | — |
| `Read(tex)` | семплирование в шейдере | → `ShaderResource` |
| `WriteColor(tex)` | color target | → `RenderTarget` |
| `WriteDepth(tex)` | depth target | → `DepthWrite` |
| `SetSideEffect()` | пас нельзя отсекать | — |

Какие бывают текстуры графа, описано в [[Ресурсы графа]].

## PassContext (execute)

| Метод | |
|---|---|
| `Commands()` | [[CommandList]] этого паса |
| `Uploads()` | [[UploadAllocator]] кадра |
| `Frame()` | [[FrameSnapshot]] (только чтение) |
| `Resolve(rg)` / `GetDesc(rg)` | реальный `TextureHandle` и описание |
| `ColorTarget(rg, clear, color)` / `DepthTarget(rg, clear, depth)` | attachment'ы для `BeginRenderPass` |
| `RecordParallel(count, batch, fn)` | батчи в отдельных списках ([[Параллельная запись]]) |

## Compile()

- **Отсечение.** Пас удаляется, если его результат никто не читает. Пас остаётся, если пишет
  в импортированную текстуру или surface, или помечен `SetSideEffect()`.
- **Уровни.** Пас попадает на уровень после последнего писателя каждого ресурса, который он
  трогает, и после читателей каждого ресурса, в который он пишет. Пасы одного уровня не
  зависят друг от друга и записываются параллельно.
- **Барьеры.** Переходы состояний вставляются в начало command list'а паса. Сам пас о них не
  думает.

Статистику графа (пасы, отсечённые пасы, уровни) показывает окно Profiler
([[Отладка рендера]]).

## См. также
- [[Ресурсы графа]], [[Фичи]], [[Пример HighlightPass]]
