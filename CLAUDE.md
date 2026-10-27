# elm: правила работы с кодом

elm — 3D-движок с редактором на C++23: рендер через Diligent (D3D12 на Windows, Vulkan на Linux),
физика Jolt, скрипты на Lua 5.4, UI редактора на ImGui. Документация на русском лежит в `docs/`
(Obsidian vault, вход `docs/Главная.md`). Прежде чем менять подсистему, прочитай её заметку.

Главная цель: код читаемый, предсказуемый и легко изменяемый. Читаемость важнее краткости и трюков.
Если правило ниже расходится с уже принятым в проекте стилем, прав проект. Спорные случаи уточняй.

## Стек и команды
- C++23, CMake ≥ 3.28. Основная платформа: Windows + MSVC (`/W4 /permissive-`), также Linux GCC/Clang. Флаги предупреждений задаёт `target_set_warnings` в `cmake/CompileOptions.cmake`.
- Зависимости (Diligent, GLFW, ImGui docking, Jolt, Tracy, glaze, Lua 5.4.7) подключены через FetchContent в `third_party/CMakeLists.txt`. Исходники качиваются в `../elm_deps`, вне репозитория. Первая конфигурация идёт долго.
- Конфигурация: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`
- Сборка: `cmake --build build --config Debug -j`
- Бинарник: `build/Debug/Engine.exe` (MSVC) или `build/Engine`. Файл `.settings` ищется вверх от текущего каталога, поэтому запускать можно из корня или из `build/`.
- Запуск с тестовой сценой: `Engine --scene <0..3> [--instances N]` (`ScenePreset`).
- Тесты: `Engine --script-selftest`. Запускает C++-проверки биндингов (`script/SelfTest.cpp`) и все
  `assets/scripts/tests/*_test.lua`. Код возврата равен числу падений. ctest в проекте нет.
- Lua API для LuaLS: `Engine --dump-script-api` перегенерирует `assets/scripts/.meta/elm.lua`. Это сгенерированный файл, руками его не правь.
- `.clang-format` и `.clang-tidy` в проекте нет. Форматируй как окружающий код этого же файла:
  отступ табом или 4 пробелами, как уже в файле. Посторонние строки не переформатируй.
- Перед завершением задачи: сборка без новых предупреждений, `--script-selftest` проходит.

## 1. Сущности выражены явно

Это главное правило проекта. Каждое понятие предметной области имеет свой тип, своё имя и своё место.
Код, документация и Lua называют его одинаково.

- **Отдельный тип на понятие.** Узел, модель, меш, компонент, событие, режим получают свой класс,
  `struct`, `enum class` или `core::Handle<Tag>`. Не кодируй понятие голым `int`/`size_t`/`void*`,
  парой, `bool`-флагом или map'ом с устной договорённостью о ключах.
- **Одно имя на сущность.** Бери имена из глоссария ниже и не вводи синонимов. Узел сцены не
  становится `Object`/`Actor`/`GameObject`, а `Model` не становится `Asset`/`Prefab`. Новое имя
  допустимо только для нового понятия.
- **Ссылка через хэндл.** Если ссылка живёт дольше одной операции, держи `NodeId`, `ModelHandler`,
  `MeshHandle` и т. п., а не указатель или индекс. Устаревший хэндл не проходит проверку поколения,
  и код это обрабатывает (`nullptr`, `false`, пропуск).
- **Владелец и поток известны.** В `///` комментарии класса сказано, кто владеет объектом, из какого
  потока его можно вызывать и что будет с невалидным хэндлом (образец: `PhysicsSystem`, `RenderSystem`).
- **Данные отдельно от систем.** Компоненты (`ScriptComponent`, `RigidBody`, `Collider`, `ModelComponent`) остаются
  простыми `struct` без логики и без ссылок на Lua/Jolt/GPU. Поведение живёт в системах.
- **Новая сущность** → строка в глоссарий ниже и раздел в соответствующей заметке `docs/`.

### Глоссарий

**Ядро** (`src/core`, `elm`, `elm::core`)

| Сущность | Что это | Файл |
|---|---|---|
| `EngineResult<T>`, `EngineError`, `ErrorCode` | результат операции, `std::expected<T, EngineError>` | `core/Error.hpp` |
| `core::Handle<Tag>` | типизированный хэндл: 24 бита индекса + 8 бит поколения, 0 значит невалидный | `core/Handler.hpp` |
| `Settings` | настройки по ключу `Category/path`, хранятся в `.settings` | `core/Settings.hpp` |
| `MessageBus` | запросы между окнами редактора (`RegisterQuery`/`Request`) | `core/MessageBus.hpp` |
| `core::JobSystem`, `TaskGroup`, `parallelFor` | пул воркеров и группы задач | `core/JobSystem.hpp` |
| `core::ThreadRole` (`Update`, `Render`) | роль потока, проверяется `ELM_ASSERT_THREAD` | `core/Threading.hpp` |
| `FileSystem`, `Path`, `AssetPaths` | пути относительно корня (`Core/FileSystem/RootPath`) | `core/FileSystem.hpp` |
| `log::Category`, `log::Severity` | лог движка, макросы `LOG_MESSAGE`/`WARNING_MESSAGE`/`ERROR_MESSAGE` | `core/Log.hpp` |

**Сцена** (`src/Scene`, `elm`)

| Сущность | Что это | Владелец / поток |
|---|---|---|
| `Scene` | узлы (плоский массив `Scene::Node`, родитель всегда раньше детей), их мировые трансформы (производный массив `WorldTransforms()`) и компоненты. Узлы выдаются только для чтения, запись идёт методами сцены по `NodeId` | `EngineApp`, update |
| `Scene::Node`, «узел» | ядро элемента сцены: `id`, родитель (`NodeId`), имя, локальный трансформ. Остальное — компоненты. В Lua это `Entity` («сущность») | `Scene` |
| `Scene::NodeDesc`, `Scene::SetParentResult` | с чем создаётся новый узел (`AddNode`); итог `SetParent` | — |
| `NodeId` | стабильная ссылка на узел (`Scene/NodeId.hpp`). Индекс узла (`size_t`) нестабилен, живёт в пределах одной операции | — |
| `NodeIdTable` | выдаёт `NodeId` и знает индекс живого узла; поколения слотов, id из Play после Stop не оживают | `Scene` |
| `IComponentStore`, `ComponentStore<T>` | компоненты одного типа по `NodeId` (sparse set), `scene.Components<T>()` | `ComponentStores` |
| `ComponentStores` | по хранилищу на каждый `ComponentType`, создаются при первом обращении | `Scene`, `SceneAsset` |
| `ComponentType`, `SceneComponent` | закрытый список типов компонентов (`Script`, `RigidBody`, `Collider`, `Model`), имена — `ComponentTypeName`/`ComponentTypeFromName` | — |
| `ModelComponent` | что узел рисует: модель (`ModelHandler`), цвет, `visible` (флаг пользователя, отсечение его не меняет). У узла без модели его нет | `Scene` |
| `ScriptComponent` | Lua-модуль узла и переопределённые `props` (`ScriptValue`) | `Scene` |
| `RigidBody`, `Collider`, `BodyType`, `ColliderShape` | физические данные узла | `Scene` |
| `CollisionEvent` | начало и конец контакта за шаг физики | `PhysicsSystem` |
| `RayHit`, `RayQuery`, `PhysicsFrameLog` | попадание луча; вызов `CastRay` и его итог; журнал кадра `PhysicsSystem::FrameLog` (лучи и начавшиеся контакты всех шагов) (`Scene/PhysicsComponents.hpp`) | `PhysicsSystem` |
| `SceneAsset`, `SceneAsset::Node` | сцена из файла до `Scene::Append`: узлы без id, родитель — индекс в ассете. Компоненты и ссылки на узлы — по `SceneAsset::NodeRef(index)`, модель в `ModelComponent` — `SceneAsset::ModelRef(index)` | вызывающий |
| `SceneSnapshot` | снимок сцены и моделей: Play делает его, Stop восстанавливает | `PlayMode` |
| `ScenePreset`, `TestScenes` | генераторы тестовых сцен | — |

**Модели и редактор** (`src/graphics`, `elm::graphics` и `elm`)

| Сущность | Что это |
|---|---|
| `Model`, `Model::Submesh`, `MeshDataPtr` | CPU-описание модели. Геометрия неизменяема и разделяется с render-потоком |
| `ModelHandler` | хэндл модели в `ModelManager` (`graphics/ModelHandler.hpp`, историческое имя, не путать с `Handle`) |
| `graphics::ModelManager` | владеет моделями сцены и их GPU-мешами (`render::Mesh`) |
| `Camera`, `CameraController`, `ViewRay` | камера вида и её управление; `ViewRay` — луч из камеры через точку вида (`GetRayThrough`) |
| `OcclusionCullingSystem`, `SoftwareDepthBuffer` | CPU-отсечение (frustum + occlusion). Сцену только читает, результат хранит по `NodeId` (`FindOccludee`) |
| `ImGuiSystem`, `IImGuiWindow`, `ImGuiWindowContext` | окна редактора и то, что им передаётся на кадр |
| `SelectedSceneInstance`, `PlayControls`, `OpenModelEditor` | сообщения между окнами через `MessageBus` (`ui/WindowMessages.hpp`). Выбранный узел — `NodeId` |
| `DebugDraw`, `DebugChannel`, `DebugPen`, `DebugLabel`, `IDebugVisualizer` | отладочная отрисовка вида (`graphics/debug`, `elm::graphics`): каналы (вкл/выкл в `Settings`), формы, текст (`DebugLabel`, рисует окно вьюпорта через ImGui), время жизни; визуализаторы состояния. Только update. Пишет в кадр сам: `WriteTo(FrameWriter&)` |
| `DebugDrawSystem` | отладочная отрисовка вида целиком: `DebugDraw` и визуализаторы, `BeginFrame`/`EndFrame`; владелец `EngineApp` |
| `WorldAxesVisualizer` | оси мира в начале координат, канал `Editor/WorldAxes` |
| `ColliderVisualizer` | каркасы `Collider` узлов по компонентам (в Edit тоже), каналы `Physics/Colliders`, `Physics/Triggers` |
| `PhysicsVisualizer` | лучи и начавшиеся контакты кадра по `PhysicsFrameLog`, каналы `Physics/Raycasts`, `Physics/Contacts` |
| `TransformGizmo`, `GizmoInput`, `GizmoAxis` | свой гизмо вьюпорта (`graphics/gizmo`): ручки перемещения, вращения, масштаба через `DebugDraw` (канал `Editor/Gizmo`), перетаскивание левой кнопкой. Режим — `GizmoSettings` |
| Edit / Play, `PlayMode` | режимы редактора: в Edit сцена — это данные, в Play работают физика и скрипты сущностей. `script::PlayMode`, владелец `EngineApp` |

**Рендер** (`src/render`, `elm::render`)

| Сущность | Что это | Поток |
|---|---|---|
| `RenderSystem` | фасад подсистемы рендера | update + render (см. его контракт) |
| `RenderResources` | создание, обновление и удаление ресурсов командами | любой |
| `TextureHandle`, `BufferHandle`, `MeshHandle`, `ShaderHandle`, `PipelineHandle`, `SurfaceId` | хэндлы ресурсов | любой |
| `render::Texture`, `render::Mesh` | RAII-владельцы ресурсов | любой |
| `FrameWriter` → `FrameSnapshot` → `FrameRing` | запись кадра на update и передача на render (3 слота) | update → render |
| `ViewPort` | offscreen-цель 3D-вида для UI | update |
| `IRenderFeature` (`ScenePass`, `DebugDrawPass`, `OverlayPass`), `RenderPipeline` | фичи, добавляющие пасы в граф | render |
| `DebugGeometry`, `DebugVertex`, `DebugBatch`, `DebugStyle` | отладочные линии и треугольники вида в мировых координатах, по стилям (`render/frame/DebugGeometry.hpp`) | update → render |
| `RenderGraph`, `PassBuilder`, `PassContext`, `FrameBlackboard` | граф пасов, зависимости, барьеры | render / воркер |
| `rhi::IRenderBackend` (`DiligentBackend`, `NullBackend`), `CommandList`, `UploadAllocator` | абстракция графического API | render |

**Остальные системы**

| Сущность | Что это | Модуль |
|---|---|---|
| `PhysicsSystem` | тела Jolt для узлов с `Collider`, по `NodeId`, только update | `physics` |
| `InputSystem`, `IInputSubscriber`, `InputContext`, `ActionBinding`, `AxisBinding` | ввод с приоритетами подписчиков и действия | `input` |
| `platform::Window` | главное окно GLFW | `platform` |
| `resmgr::Serializer`, `GltfExporter`, `JsonValue` | `.model`/`.scene` (JSON через glaze), экспорт glTF | `resmgr` |
| `script::ScriptSystem` | менеджер скриптов: Lua-машина, сборка частей и биндингов, порядок в кадре, Shutdown. Части внутренние для модуля | `script` |
| `EngineContext`, `ScriptTime`, `PlayRequests` | системы движка для скриптов, время кадра, запросы Play/Stop | `script` |
| `ModulePaths`, `ModuleLoader`, `LoadedModule` | имя модуля ↔ файл; `require` и загруженные модули | `script/module` |
| главный модуль (`MainModule`, `MainState`) | модуль с хуками `main.on_*` | `script/module` |
| `HotReload` | перезагрузка изменённых модулей с патчем таблицы | `script/reload` |
| скрипт сущности (`EntityScript`, `EntityScripts`, `EntityScriptList`) | экземпляр класса на узле с `ScriptComponent` и их проходы | `script/entity` |
| `ScriptClass`, `ScriptDefaults` | модуль как класс скрипта; props по умолчанию для Inspector | `script/entity` |
| `ScriptConsole` | Lua-консоль окна Scripts | `script/console` |
| `Entity`, `ModelRef`, `ScriptProps` | как узлы, модели и props видны из Lua | `script/values` |
| `lua::State`, `lua::Module`, `lua::Class`, `ApiRegistry` | своя обёртка над Lua C API и реестр API для LuaLS | `script/lua` |
| `bindings::Register*`, `ComponentKind` | Lua API по модулям, у каждого свой контекст в `lua::State`; `ComponentKind` — компонент для Lua по `ComponentType` | `script/bindings` |
| `elm.debug`, класс `DebugChannel` | отладочная отрисовка из Lua в каналы `Script/*`; Stop чистит их примитивы со временем (`ClearScriptDebugDraw`) | `script/bindings` |

## 2. Модули и зависимости

Зависимости направлены в одну сторону. Новых связей против стрелок не добавляй.

```
core, math                   основа, ни от кого не зависят (core: Tracy)
render     → core, math      Diligent только PRIVATE
platform   → render          GLFW
Scene      → core, math, render
resmgr     → Scene           glaze PRIVATE
physics    → Scene           Jolt PRIVATE
graphics   → render, platform, resmgr (+ ImGui)
input      → glfw
script     → core, lua       остальные модули PRIVATE
Engine     (EngineApp, main.cpp) собирает всё
```

Каждый модуль собирается своим `engine_add_library` в `src/<module>/CMakeLists.txt`. Новый файл
добавь в список исходников модуля.

Внутри `script` действуют слои (`docs/script/Скрипты на Lua.md`): зависимость только вниз, части
одного слоя друг о друге не знают, связи между ними задаёт `ScriptSystem`. Внутренние части модуля
(`module/`, `reload/`, `entity/`, `console/`, `values/`, `bindings/`, `ui/`) не подключаются вне `src/script/`:
снаружи видны `ScriptSystem`, `EngineContext`, `PlayMode` и `RunSelfTests`.

Известные нарушения, их не расширяй: заголовки модуля Scene подключают заголовки `graphics` (`Scene.hpp` и
`ModelComponent.hpp` — `ModelHandler.hpp`, `SceneAsset.hpp` — `Model.hpp`, `SceneSnapshot.hpp` и `TestScenes.hpp` — `ModelManager.hpp`,
`Transform.hpp` — `culling/MathTypes.hpp`; цикл Scene ↔ graphics через resmgr); `using namespace math;` в
`math/Primitivs.hpp`, `Scene/TestScenes.hpp`, `graphics/culling/MathTypes.hpp`.

### Границы, которые нельзя нарушать
- **Потоки.** Update-поток собирает кадр N+1, пока render-поток рисует кадр N. Update не вызывает
  графический API, а пишет команды в `RenderResources` и данные в `FrameWriter`. Draw-API доступен
  только внутри паса. Таблица допустимых вызовов: `docs/render/Правила потоков рендера.md`.
  Новые API с привязкой к потоку помечай `ELM_ASSERT_THREAD(Update)` / `ELM_ASSERT_THREAD(Render)`.
- **Diligent** виден только в `render/backend/diligent/`. Выше `rhi::IRenderBackend` его типов нет.
- **Jolt** виден только в `physics/PhysicsSystem.cpp`. Линковка PRIVATE обязательна: Jolt
  выставляет `_HAS_EXCEPTIONS=0`, а движку нужны исключения.
- **Lua** подключается только через `script/lua/Lua.hpp`, не через `lua.hpp`. Lua собрана как C++,
  поэтому `lua_error` бросает исключение и разматывает C++-кадры биндингов.
- **Время жизни.** RAII-ресурсы рендера (`Texture`, `Mesh`, `ViewPort`, `ModelManager`) уничтожаются
  до `RenderSystem::Shutdown()`.
- **Формат файлов.** Меняешь формат `.scene`/`.model`: подними `formatVersion`, сохрани чтение
  старых версий и обнови `docs/scene/Файл сцены.md`.

## 3. Ответственность и структура (SRP)
- Один класс, функция, файл = одна причина для изменения. Если в имени функции нужен «и»
  (`LoadAndApply`), раздели её.
- Функция: желательно до 30 строк, не глубже 3 уровней вложенности, не больше 4 параметров.
  Больше параметров собери в структуру (`ImGuiWindowContext`, `EngineContext`, `*Desc`).
- Не смешивай в классе логику, ввод-вывод, UI и хранение. Окна (`*Window`) показывают и правят,
  системы (`*System`) считают, `resmgr` читает и пишет файлы.
- Никаких `utils.h`/`helpers.h`/`common.h`: файл называется по назначению. Исключение — уже
  существующий `render/backend/diligent/Utils.*`, внутренний для бэкенда.
- Зависимости приходят снаружи: через конструктор (`ModelManager(RenderResources&)`), `Init`
  или контекст кадра. `core::JobSystem::Get()` — единственный глобальный синглтон, новых не добавляй.
- Один основной класс = пара `ClassName.hpp` / `ClassName.cpp`.
- **Многострочные тела функций в `.hpp` запрещены.** Тело функции в заголовке допустимо только у:
  - шаблонов (шаблонные функции и методы шаблонных классов);
  - inline-функций: однострочных методов в теле класса (геттер, сеттер, `operator bool`,
    делегирование) и функций, которые обязаны быть в заголовке (`constexpr`, явный `inline`).
  Всё остальное уходит в `.cpp`, включая конструкторы и методы из нескольких строк, даже короткие.
  Уже существующие тела в заголовках без запроса не переноси.
- `#pragma once`. В заголовках минимум `#include`, где можно — forward declaration
  (как `namespace JPH { class PhysicsSystem; }` в `PhysicsSystem.hpp`).
- Include-пути от `src/`: `#include "render/api/Handles.hpp"`. Порядок: свой заголовок; проектные,
  сгруппированные по модулю; сторонние; стандартная библиотека. Группы разделены пустой строкой.
- Детали реализации прячь в анонимный namespace в `.cpp`.

## 4. Именование (как принято в проекте)
- Типы: `PascalCase`. Интерфейсы с префиксом `I`: `IRenderFeature`, `IComponentStore`, `IImGuiWindow`.
- Методы: `PascalCase`, с глагола: `Init`, `BeginFrame`, `AddNode`, `GetName`.
  Булевы методы читаются как вопрос: `IsValid`, `HasBody`, `Contains`.
- Свободные функции: `PascalCase` (`MakeUnexpected`, `ComputeBounds`, `RunSelfTests`).
  В `core::`, `log::`, `memory::` исторически `camelCase` (`registerThread`, `parallelFor`):
  новые функции там называй как соседние.
- Приватные поля: `m_camelCase`. Публичные поля `struct`: `camelCase` без префикса.
- Константы: `static constexpr` в `PascalCase` (`NoNode`, `MaxIndex`, `MainSurface`, `EnabledSetting`).
  Префикс `k` не используется.
- Хэндлы: `XxxHandle` в render, `NodeId` в сцене, `ModelHandler` в graphics; теги `XxxTag` объявлены
  без определения.
- Макросы: только с префиксом `ELM_` и только для логов, ассертов и платформы.
- Namespace: всё в `elm` (`elm::core`, `elm::render`, `elm::render::rhi`, `elm::graphics`,
  `elm::platform`, `elm::resmgr`, `elm::script`, `elm::script::lua`, `elm::script::bindings`).
  Математика исторически в корневом `math`. Закрывающий комментарий называет namespace верно:
  `} // namespace elm::render`. Встречающееся `// namespace Engine` не копируй.
- Lua-сторона: `snake_case` (`elm.scene.spawn`, `on_start`, `props`). Имена компонентов в Lua и
  в файле сцены: `Script`, `RigidBody`, `Collider`; в файле есть ещё `Model` (в Lua — `entity.model/color/visible`).
- Имена говорят о назначении: никаких `data`, `tmp`, `x1`, `doStuff`. Сокращения только общепринятые.

## 5. Современный C++ и ресурсы
- Контейнеры, строки и умные указатели бери из `core/Std.hpp`: `String`, `StringView`, `Vector`,
  `UnorderedMap`, `Set`, `Deque`, `UniquePtr`/`MakeUnique`, `SharedPtr`/`MakeShared`. Они идут
  через аллокатор движка, и память видна в `memory::getStatistic()`. Голые `std::vector`/`std::string`/
  `std::unique_ptr` в коде движка не используй. `std::span`, `std::optional`, `std::variant`,
  `std::function` используются напрямую.
- Математика: `math::Vector3/Vector4/Quaternion/Matrix4x4`, `Size`, `AABB`, `MeshData` из `src/math`.
- RAII везде. Никаких голых `new`/`delete`/`malloc`/`free`.
- Невладеющий доступ: `T&` (обязателен), `T*` (может отсутствовать), `std::span`, `StringView`.
- Параметры: дешёвые типы и хэндлы по значению; только чтение `const T&`; изменение `T&`;
  передача владения по значению с `std::move`.
- Системы с идентичностью (`*System`, `EngineApp`) явно запрещают копирование и перемещение
  (`= delete` для всех четырёх). Значимые типы следуют правилу нуля.
- `const` по умолчанию. `[[nodiscard]]` на всём, что возвращает результат, хэндл или найденный
  объект. `override`/`final` обязательны. Однопараметрические конструкторы `explicit`.
- Функции, которые могут не выполниться: `[[nodiscard]] auto Init(...) -> EngineResult<void>`.
- `enum class`; у компонентных enum указан тип (`: uint8_t`). Имена для файлов и Lua — парой
  `constexpr` функций `XxxName` / `XxxFromName` (см. `PhysicsComponents.hpp`).
- Инициализация через `{}`, поля всегда с инициализатором (`float mass { 1.0f };`).
- Только именованные касты. `reinterpret_cast` только в бэкенде, Lua и `void*`-API, с комментарием-обоснованием.
- `auto`, только когда тип очевиден из правой части.

## 6. Ошибки
- API движка возвращает `EngineResult<T>`. Ошибку создавай через
  `MakeUnexpected(ErrorCode, message, log::Category, source)` из `core/Unexpected.hpp`: она сразу
  пишет в лог. Ожидаемое отсутствие: `nullptr`/`std::optional`/`false` (`Find`, `Get`, `Remove`).
- Исключения только в двух местах: ошибки Lua (`luaL_error` в биндингах) и ошибки программиста
  в инфраструктуре (`std::logic_error` в `MessageBus`). Через render-поток и воркеры исключения не идут.
- Инварианты: `ELM_ASSERT(cond && "почему")`, принадлежность потоку: `ELM_ASSERT_THREAD(Role)`.
  `<cassert>` не используй.
- Предусловия проверяй на границе модуля: в публичном API, в биндингах (`RequireSystem`).
- Ранний возврат вместо вложенности. Деструкторы и move-операции `noexcept`.
- Не игнорируй `EngineResult`: обработай или передай выше.

## 7. Читаемость и документация
- Код объясняет себя сам. Сначала имена и структура, потом комментарий.
- Комментарии и `///` пиши по-английски, коротко и про контракт: владение, поток, поведение при
  невалидном хэндле, «почему», а не «что». Закомментированный код не оставляй.
- Документация в `docs/` на русском, с wiki-ссылками `[[Заметка]]`. Изменил поведение — обнови
  заметку. Новая подсистема — заметка и ссылка из `docs/Главная.md`.
- Никаких магических чисел и строк: именованные константы. Ключи настроек объявлены как
  `static constexpr StringView XxxSetting` и регистрируются через `Settings::Register`.
- Одна переменная на строку, объявление рядом с использованием.
- Без глобального изменяемого состояния. Без абстракций «на будущее» (YAGNI). DRY — после третьего повтора.
- Шаблоны только когда упрощают код, с ограничением через `concepts`.

## 8. Классы и интерфейсы
- Данные и поведение, которые меняются вместе, лежат вместе. Инварианты держит сам класс,
  поля `private`. `struct` — для агрегатов без инвариантов: компоненты, `*Desc`, сообщения, статистика.
- Интерфейс: `I*`, чисто виртуальные методы, виртуальный деструктор, без данных, узкий.
  (`IImGuiWindow` с данными — исключение, новые интерфейсы так не делай.)
- Композиция вместо наследования. Наследование только для «является».

## 9. Многопоточность
- Изменяемое состояние между потоками не разделяй. Update → render передаётся снимком кадра
  (`FrameSnapshot`) и очередью команд ресурсов.
- Блокировки: `std::lock_guard` / `std::scoped_lock` / `std::unique_lock`. Мьютекс лежит рядом
  с данными и наружу не выставляется (`mutable std::mutex m_mutex`).
- Параллелизм внутри кадра: `core::JobSystem` / `TaskGroup` / `parallelFor`, свои потоки не создавай.
  У потоков с фиксированной ролью регистрируй её (`core::registerThread`).
- Атомарные счётчики через `std::atomic`.

## 10. Тесты
- Тесты — это `Engine --script-selftest`. GoogleTest/Catch2 в проекте нет, без согласования не добавляй.
- Lua-тест: файл `assets/scripts/tests/<область>_test.lua` возвращает таблицу `tests` с функциями,
  проверки через `require "tests.expect"` (`expect.eq`, `expect.near`, `expect.error`), фикстуры
  в `tests/fixtures/`. Имя теста описывает сценарий: `tests.vector3_construction`.
- C++-проверки биндингов и тестовый модуль `elm_test` лежат в `script/SelfTest.cpp`.
- Изменил биндинги или Lua API: добавь тест и перегенерируй `assets/scripts/.meta/elm.lua`.
- Для рендера, culling и UI автотестов нет. Проверяй запуском (`Engine --scene N`) и прямо пиши,
  что проверено вручную, а что нет.
- Исправление бага: сначала тест, который его воспроизводит, если это достижимо через self-test.

## 11. Процесс работы агента
1. Перед кодом изучи существующий код, заметку в `docs/` и стиль файла. Не изобретай заново.
2. Для нетривиальных задач сначала короткий план: какие сущности и файлы затронуты и за что отвечает каждый.
3. Минимальные изменения под задачу. Постороннее без запроса не рефактори, включая известные нарушения выше.
4. Одна логическая правка = один набор связанных изменений.
5. После правок: сборка без новых предупреждений, `--script-selftest`, обновлённые `docs/`.
6. Новые зависимости только по согласованию.
7. Требование неясно или правило конфликтует с задачей: спроси, не угадывай.
8. Сообщения коммитов: коротко, по-английски, в повелительном наклонении (`add lua bindings`).
   `.settings`, `scene.scene`, `imgui.ini` — рабочие файлы пользователя, без просьбы их не коммить.

## Чего делать нельзя
- Голые `new`/`delete`, C-style касты, `using namespace` в новых заголовках.
- Многострочные тела функций в `.hpp`, кроме шаблонов и inline-функций.
- Синонимы для существующих сущностей и понятия, закодированные голыми `int`/`bool`/индексами.
- Типы Diligent вне `render/backend/diligent/`, заголовки Jolt вне `PhysicsSystem.cpp`, `lua.hpp` напрямую.
- Вызовы графического API из update-потока, исключения через render-поток.
- Глобальные изменяемые переменные и новые синглтоны.
- Функции и классы «на все случаи» с флагами-переключателями поведения.
- Подавление предупреждений компилятора вместо исправления.
- Правка сгенерированных и сторонних файлов: `assets/scripts/.meta/elm.lua`, `build/`, `../elm_deps/`.
