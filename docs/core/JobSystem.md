---
tags: [core, threading]
aliases: [TaskGroup, parallelFor]
---
# JobSystem

Файл: `core/JobSystem.hpp`. Пул потоков на весь процесс. Если пул не инициализирован, задачи
выполняются сразу на вызывающем потоке.

```cpp
core::JobSystem::Get().Init();      // 0 = hardware_concurrency - 2 воркеров (минимум 1)
...
core::JobSystem::Get().Shutdown();
```

## TaskGroup

```cpp
core::TaskGroup group;
group.Run([] { WorkA(); });
group.Run([] { WorkB(); });
group.Wait();                       // пока ждёт, сама выполняет задачи из очереди
```

- `Wait()` помогает выполнять очередь. Поэтому группы можно вкладывать и ждать прямо
  на воркерах.
- Деструктор вызывает `Wait()`.
- Группу можно держать на стеке: `Wait()` возвращается только после того, как последний
  воркер закончит работу с группой.

## parallelFor

```cpp
core::parallelFor(count, /*batch*/ 256, [&](size_t begin, size_t end) {
    for (size_t i = begin; i < end; ++i) Process(i);
});
```

Первый батч выполняется на вызывающем потоке.

## Где используется

- [[Параллельная запись]] пасов рендера
- [[Загрузка ресурсов в фоне]]

## См. также
- [[Роли потоков]]
