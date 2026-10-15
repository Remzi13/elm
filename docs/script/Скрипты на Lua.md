---
tags: [script, lua]
aliases: [ScriptSystem, Lua, lua::Class, lua::Module, EntityScripts, HotReload, ScriptConsole]
---
# Скрипты на Lua

Файлы: `script/*`. Единственная зависимость — Lua 5.4.7 (`third_party/CMakeLists.txt`).
Обёртка над C API своя (`script/lua/*`), sol2 и подобных библиотек нет.

Lua собирается **как C++**. Поэтому `lua_error` бросает исключение, а не делает `longjmp`,
и ошибка в биндинге вызывает деструкторы C++-объектов. Заголовки подключаются через
`script/lua/Lua.hpp`, никогда через `lua.hpp`: его `extern "C"` не совпадёт с линковкой.

## Устройство модуля script

`ScriptSystem` — только менеджер: открывает Lua-машину, собирает части и биндинги, ведёт их по
кадру (`Update`, `FixedUpdate`) и завершает в правильном порядке. Работу делают части, они
внутренние для модуля: снаружи видны только `ScriptSystem`, `EngineContext` и `PlayMode`.

Части разложены по слоям, зависимость идёт только вниз. Части одного слоя друг о друге не знают:
связи между ними задаёт `ScriptSystem` (колбэк после перезагрузки модуля, порядок вызовов в кадре).

| Слой | Части | Что делают |
|---|---|---|
| 5 | `ScriptSystem` (`script/ScriptSystem.*`) | жизненный цикл, сборка частей, регистрация биндингов, окна редактора |
| 4 | `bindings/*`, `ui/*` | адаптеры: Lua API и окна редактора. Каждый файл биндингов и каждая панель зависят только от того, что показывают |
| 3 | `MainModule`, `EntityScripts` (+ `EntityScriptList`), `ScriptDefaults`, `HotReload`, `ScriptConsole` | главный модуль, скрипты сущностей, props для Inspector, перезагрузка, консоль |
| 2 | `ScriptClass` (`entity/`) | модуль как класс скрипта: проверка таблицы и props, новый экземпляр |
| 1 | `ModuleLoader` (`module/`), `values/` | `require` и список загруженных модулей; `Entity`, `ModelRef`, `ScriptValue` ↔ Lua |
| 0 | `ModulePaths` (`module/`), `lua/` | имя модуля ↔ файл; обёртка над C API |

- Части живут столько же, сколько `ScriptSystem`. Lua-машина `lua::State` пересоздаётся при
  перезапуске (`lua::State::Reset`), части получают её ссылку в конструкторе, а в `Init`/`Shutdown`
  ставят свои Lua-хелперы и освобождают ссылки на Lua-объекты.
- Биндинги не знают `ScriptSystem`. Каждый Lua-модуль регистрируется своей функцией
  (`bindings::RegisterScene(state, scene, models, entityScripts)` и т. п.) и кладёт свои зависимости
  в `lua::State` под своим типом (`SetContext<T>`/`Context<T>(L)`).
- Запросы вверх (перезапуск скриптов, Play/Stop) нижние слои получают функциями, не зная, кто их выполняет.
- Self-тесты проверяют части напрямую: `SelfTestAccess` — друг `ScriptSystem`.

## Lua-машина

- Одна `lua_State`, живёт на update-потоке (`ELM_ASSERT_THREAD(Update)`).
- Память идёт через аллокатор движка и видна в `memory::getStatistic()`.
- `require("a.b")` ищет `assets/scripts/a/b.lua`, затем `a/b/init.lua`. Поиск идёт только
  через файловую систему движка: `package.path`, нативных модулей, `dofile` и `loadfile` нет.
- Нет `io` и `os`. `load` принимает только текст: байткод не проверяется Lua и может уронить VM.
  Библиотека `debug` открыта только в debug-сборке.
- `print` и `elm.log.info/warn/error` пишут в лог движка (категория `Script`) с файлом и строкой.

Главный модуль задаётся настройкой `Script/MainModule` (по умолчанию `main`). Он возвращает
таблицу с хуками:

```lua
local main = {}
function main.on_start() end
function main.on_update(dt) end        -- Play: после input, до пересчёта трансформов и culling
function main.on_fixed_update(dt) end  -- Play: после шага физики
function main.on_play() end            -- нажали Play: снимок сцены уже сделан
function main.on_stop() end            -- нажали Stop: до того как сцена вернётся
function main.on_shutdown() end
return main
```

Главный модуль стартует в начале `EngineApp::Run`, после того как загружена начальная сцена
(`--scene`). Движок начинает в режиме Edit ([[Редактор]]): там вызываются только `on_start`,
`on_reload` и `build_scene`, а `on_update` и `on_fixed_update` — только в Play.

### main.lua — точка входа

`assets/scripts/main.lua` описывает сцену функцией `main.build_scene()`. Её вызывают `on_start`
и `on_reload`, поэтому каждое сохранение файла собирает сцену заново по новому коду.
Кнопка «Rebuild scene» в окне Scripts и команда `/scene` в его консоли вызывают
`build_scene()` без перезагрузки модуля (`MainModule::CallFunction`).

`BUILD_SCENE = false` в начале файла оставляет сцену из `--scene` или окна Scene Hierarchy. Если хук упал, ошибка с traceback уходит в лог, и хуки главного модуля
не вызываются до перезагрузки модуля или перезапуска скриптов.

## Скрипты сущностей

Логика отдельного объекта (NPC, дверь, ловушка) живёт в компоненте `Script` его узла сцены.
Модуль скрипта возвращает **класс**: методы и значения `props` по умолчанию. Каждая сущность
с компонентом получает свой **экземпляр** `self`.

```lua
-- assets/scripts/npc/guard.lua
local Guard = { props = { speed = 2.5, view_distance = 8 } }    -- значения по умолчанию

function Guard:on_start() self.post = self.entity.position end   -- self.entity — сама сущность
function Guard:on_update(dt) --[[ ... ]] end
return Guard
```

```lua
local npc = elm.scene.spawn("Guard 1", cube)
local guard = npc:add("Script", { module = "npc.guard", props = { speed = 3 } })   -- или npc:add("Script", "npc.guard")
print(guard.speed, npc.script == guard)    -- 3  true: props лежат в self
npc:has("Script")
npc:remove("Script")                       -- on_destroy, сущность остаётся
for _, e in ipairs(elm.scene.with("Script")) do print(e.script) end
```

Хуки, все необязательные:

| Хук | Когда вызывается |
|---|---|
| `on_start(self)` | в начале следующего `Update` в режиме Play, до первого `on_update`. В Edit скрипты сущностей не стартуют |
| `on_update(self, dt)` | каждый кадр, после `on_update` главного модуля |
| `on_fixed_update(self, dt)` | после шага физики, после главного модуля |
| `on_destroy(self)` | `destroy()`, `remove("Script")`, `clear_scene`/`load_scene`/`load_test_scene`, перезапуск скриптов; только после `on_start` |
| `on_script_reload(self)` | после перезагрузки модуля этого скрипта |
| `on_collision_enter(self, other, contact)` | после шага физики: тела начали касаться. `contact = { point, normal }`, нормаль от `self` к `other` |
| `on_collision_exit(self, other)` | тела перестали касаться |
| `on_trigger_enter(self, other)`, `on_trigger_exit(self, other)` | одно из двух — триггер: вошло, вышло |

Как это устроено:
- **Данные** — `ScriptComponent` (`Scene/ScriptComponent.hpp`): имя модуля и props, которые
  отличаются от значений по умолчанию. Компонент хранится в сцене (`scene.Components<ScriptComponent>()`)
  без Lua, поэтому переживает перезапуск скриптов: экземпляры создаются из него заново. Он же
  сохраняется в [[Файл сцены]].
- **Экземпляр** — `EntityScript` в `EntityScripts` (`script/entity/*`): таблица
  `self = { entity, <props> }`, у которой класс стоит в `__index`. Поэтому работают и
  унаследованные методы (`setmetatable(Guard, { __index = require "npc.base" })`).
- `entity:add` создаёт экземпляр сразу. Ошибка в модуле или в props поднимается в месте вызова,
  и компонент не добавляется. Компоненту, добавленному из C++, экземпляр создаёт следующий `Update`.
- **Типы props:** `boolean`, `number`, `string`, `Vector3`, `Vector4`, `Entity`. Это то, что
  сохраняется в файл сцены и сможет показать редактор. Переопределение должно совпадать по типу со значением по
  умолчанию. Векторы из `props` копируются в каждый экземпляр. Остальное состояние (таблицы,
  ссылки) держите в полях `self` и заполняйте в `on_start`.
- Один `Script` на сущность. Поведение из частей собирается внутри Lua: `require`, наследование.
- **Ошибка в хуке останавливает только этот экземпляр.** Текст уходит в лог, одинаковые ошибки
  разных экземпляров — один раз. Любая успешная перезагрузка модуля снимает остановку. Компонент
  с модулем, который не загрузился, пробует создаться снова.
- **Hot reload:** таблица класса меняется на месте, все экземпляры сразу выполняют новый код и
  сохраняют поля `self`. Не называйте метод экземпляра `on_reload`: так называется хук модуля
  `on_reload(previous)`, его вызовут с полями старой таблицы.
- `on_destroy` при `entity:destroy()` видит сущность живой. Если узел удалили в обход скриптов
  (окно сцены, C++), `on_destroy` вызывается на следующем проходе, и `self.entity.valid` уже `false`.
- Экземпляры вызываются в порядке создания. Сущности, созданные главным модулем, стартуют в том же
  кадре. Созданные внутри `on_start`/`on_update` других скриптов стартуют в следующем.
- Настройка `Script/Enabled = false` выключает и скрипты сущностей.

Пример: `assets/scripts/demo/npcs.lua` (`Script/MainModule = demo.npcs`) спавнит стражей
`assets/scripts/npc/guard.lua`. Они патрулируют у своих постов и гонятся за кубом игрока, которого
двигают стрелки. Стражи и игрок — кинематические тела, они расталкивают ящики, а ящик между
стражем и игроком прячет игрока (луч в `Guard:can_see`).

### Физика из Lua

Компоненты `RigidBody` и `Collider` добавляются так же, как `Script`. `add` возвращает их хэндл,
он же — `entity.rigidbody` и `entity.collider` (`nil` без компонента). Поля хэндла читают и пишут
компонент в сцене. Тело строится заново на следующем шаге физики. Подробности — [[Физика]].

```lua
local crate = elm.scene.spawn("Crate", elm.models.cube(1))
crate:add("Collider")                         -- без размеров коллайдер повторяет модель
local body = crate:add("RigidBody", { mass = 2, restitution = 0.3 })
body.velocity = Vector3(0, 5, 0)              -- тело есть сразу после add
body:add_impulse(Vector3(10, 0, 0))

local zone = elm.scene.spawn("Zone")
zone:add("Collider", { shape = "sphere", radius = 3, trigger = true })
zone:add("Script", "gameplay.launch_pad")     -- on_trigger_enter(self, other)
```

| Хэндл | Поля и методы |
|---|---|
| `RigidBody` | `type` (`"static"`, `"kinematic"`, `"dynamic"`), `mass`, `friction`, `restitution` (0..1), `linear_damping`, `angular_damping` (0..1), `gravity_scale`; `velocity`, `angular_velocity`, `add_force(force)`, `add_impulse(impulse)` — только у динамических; `valid`, `entity` |
| `Collider` | `shape` (`"box"`, `"sphere"`, `"capsule"`), `half_extents`, `radius`, `half_height`, `offset`, `trigger`; `valid`, `entity` |

- Поля init — те же поля хэндла. Неизвестное поле или неверное значение — ошибка, и компонент
  не добавляется.
- `Collider` без размеров повторяет модель сущности: коробка по её границам, радиус сферы и капсулы
  по X/Z. Плоская модель (`elm.models.plane`) получает коробку толщиной 0.5 под собой.
- `velocity`, силы и импульсы нужны телу, а тело есть только с `Collider`. Без него — ошибка.
- Кинематическое тело двигают через `entity.position`: физика сама доводит до него тело со
  скоростью и толкает динамические тела по пути.
- `elm.physics.raycast(origin, direction, max_distance?, ignore?)` возвращает `entity, point,
  normal, distance` ближайшего твёрдого тела (триггеры пропускаются) или ничего. `ignore` — сущность,
  из которой выходит луч. Ещё есть `gravity()`, `set_gravity(v)`, `body_count()`.

Пример: `assets/scripts/demo/physics.lua` (`Script/MainModule = demo.physics`): пирамида ящиков,
батут-триггер `gameplay/launch_pad.lua`, Space бросает камни `gameplay/projectile.lua`, которые
вспыхивают в `on_collision_enter`.

## Hot reload

Настройка `Script/HotReload` (по умолчанию включена). Файлы загруженных модулей
проверяются четыре раза в секунду. Изменение применяется, когда два опроса подряд видят
одно и то же время файла: так редактор успевает дописать файл. Перезагрузка выполняется
в начале `ScriptSystem::Update` (`HotReload::Poll`), вне хуков.

Что происходит при перезагрузке модуля:
1. Файл выполняется заново, как при `require`.
2. Если компиляция или выполнение упали, ошибка попадает в лог и в окно Scripts,
   **а старая версия продолжает работать**.
3. Новая таблица модуля копируется в старую, поэтому все, кто держит модуль
   (результаты `require`, главный модуль), видят новые функции. Поля, которых нет в новой
   версии, удаляются. Upvalue функций модуля, указывавшие на новую таблицу (`local M = {}`),
   переводятся на старую, так что код модуля и внешний мир видят одну таблицу.
4. Вызывается `on_reload(previous)` новой версии: в `previous` лежат поля старой.

Локальные переменные модуля при перезагрузке начинаются заново. Состояние, которое должно
пережить перезагрузку, нужно держать в `elm.script.persist(key, default)` (живёт до перезапуска)
или переносить в `on_reload`:

```lua
local state = elm.script.persist("demo.spinner", { cubes = {}, direction = 1 })
```

Модуль, который не загрузился при старте (синтаксическая ошибка в `main.lua`), тоже
отслеживается: после исправления он загружается сам, и у главного модуля вызывается `on_start`.

Перезапуск (`elm.script.restart()`, кнопка Restart) создаёт новую Lua-машину на следующем
кадре: у старой вызывается `on_shutdown`, все модули и `persist` загружаются заново.

## Окно Scripts

`script/ui/ScriptWindow.*`, пункт «Scripts» в меню Windows. Окно только раскладывает свои части и выполняет
`/`-команды консоли. Каждая панель работает только со своими частями модуля: `ScriptStatusBar` (панель сверху),
`ConsolePanel` (`ScriptConsole`), `ScriptEditor`, `ModulesPanel` (`ModuleLoader`, `HotReload`), `ApiPanel` (`lua::State`).
Окна создаёт `ScriptSystem::CreateEditorWindows`, `EngineApp` только добавляет их в `ImGuiSystem`.

- **Панель сверху:** состояние главного модуля (running / stopped с текстом ошибки и кнопкой
  перезагрузки / not running), память Lua, флажок Hot reload, кнопки «Reload changed» и «Restart».
- **Console:** строка Lua выполняется в живом движке. Для выражения печатается его значение,
  `print` внутри команды выводится сюда же. Tab дополняет имена (`elm.sc` → `elm.scene`,
  `v:` → методы объекта), стрелки листают историю. Команды консоли: `/help`, `/clear`,
  `/modules`, `/reload [module]`, `/restart`, `/scene` (пересобрать сцену), `/edit [module]`.
- **Editor:** редактор скриптов.
  - Слева дерево `.lua`-файлов (папки на `.`, например `.meta`, пропускаются) и создание нового
    файла (`folder/name`).
  - Справа вкладки открытых файлов, несохранённые помечены.
  - **Ctrl+S** (или Save) записывает файл и сразу перезагружает его модуль. Результат пишется
    над текстом. Если перезагрузка или `on_reload` упали, текст ошибки показывается красным
    с кнопкой «Go to line N», а старая версия модуля продолжает работать.
  - Файл, изменённый на диске другим редактором, подхватывается сам. Если в нём есть
    несохранённые правки, редактор спрашивает, чью версию оставить.
  - Внизу позиция курсора `Ln, Col`.
  - Подсветка синтаксиса Lua: ключевые слова, `nil`/`true`/`false`, строки (в том числе `[[ ]]`),
    комментарии (в том числе `--[[ ]]`), числа, стандартные глобалы и `elm`, имена функций после
    `function`. Слева номера строк, текущая строка выделена. Строка ошибки подсвечивается красным,
    в том числе сразу при открытии файла модуля, который не загрузился.
  - Как это устроено: `InputTextMultiline` из ImGui умеет только один цвет текста. Поле рисует
    текст прозрачным, а `ScriptEditor::DrawHighlight` поверх в том же дочернем окне рисует цветные
    токены (`script/ui/LuaHighlighter.*`), курсор и номера строк по тем же координатам и прокрутке.
    Используется `imgui_internal.h`, поэтому при обновлении ImGui это место нужно проверить.
- **Modules:** загруженные модули, их состояние или ошибка, число перезагрузок, кнопки Reload и
  Edit. Клик по ошибке открывает файл на её строке.
- **API:** всё, что дают биндинги, с типами и фильтром. Клик вставляет вызов в консоль.

Команды движка для консоли и скриптов:

| Модуль | Что даёт |
|---|---|
| `elm.engine` | `load_test_scene(preset, instances?)`, `clear_scene()`, `save_scene(file)`, `load_scene(file)` (файлы относительно `assets/scenes`); `play()`, `stop()`, `is_playing()` ([[Редактор]]) |
| `elm.script` | `reload(module)`, `reload_changed()`, `restart()`, `modules()`, `persist(key, default)`, `memory()`, `engine_memory()` |

## Обёртка

```cpp
#include "script/lua/Bind.hpp"

lua::Module module(state, "elm.math");            // глобальная таблица elm.math, она же require "elm.math"
module.Function<&Lerp>("lerp", { "a", "b", "t" })  // функция, лямбда без захвата или int(lua_State*)
      .Value("pi", 3.14159f);

lua::Class<Vector3>(module, "Vector3")
    .Constructor<float, float, float>({ "x", "y", "z" })  // Vector3(1, 2, 3) и Vector3.new(...)
    .Field<&Vector3::x>("x")                              // const-поле доступно только на чтение
    .Property<&GetLength>("length")                       // Property<Get, Set> для записи
    .Method<&Vector3::Dot>("dot", { "other" })            // или свободная функция (T&, ...)
    .StaticFunction<&Zero>("zero")
    .Meta<&Add>("__add");                                 // __eq ставится сам для сравнимых типов
```

- Аргументы и результаты: `bool`, целые (с проверкой диапазона), `float`/`double`, enum,
  `String`/`StringView`/`const char*`, `std::optional<T>` (nil), зарегистрированные классы,
  `lua::Ref`/`lua::FunctionRef`.
- `std::tuple` возвращает несколько значений. Неудачный `EngineResult` и C++-исключение
  становятся ошибкой Lua.
- Параметр `lua_State*` получает вызывающий стейт и не занимает слот аргумента.
- Классы хранятся в userdata **по значению**. `T&`-параметр меняет сам Lua-объект. Поле
  классового типа возвращается копией, поэтому `a.b.x = 1` меняет только копию.
- Записать неизвестное поле нельзя: это ошибка.
- `lua::FunctionRef::Call<R>(args...)` вызывает Lua из C++ в защищённом режиме и возвращает
  `EngineResult<R>`. Все `Ref` нужно освободить до уничтожения `State`.

## Модули движка

Регистрируются в `script/bindings/*`, по файлу на модуль. Системы движка приходят в `ScriptSystem::Init` (`EngineContext`).
Если какой-то системы нет (инструменты, самотесты), её функции бросают ошибку «not available here».

| Модуль | Что даёт |
|---|---|
| `elm.log` | `info`, `warn`, `error` (и `print`) |
| `elm.time` | `delta()`, `fixed_delta()`, `now()`, `frame()` |
| `elm.settings` | `get("Script/MainModule")`, `set(key, value)`: тип значения как при регистрации |
| `elm.math` | `Vector3`, `Vector4`, `Quaternion`, `Matrix4x4`, `lerp`, `clamp`, `radians`, `degrees`, `pi` |
| `elm.input` | `is_down/is_pressed/is_released(key)`, `is_mouse_*`, `mouse_position()`, `mouse_delta()`, `axis(name)`; константы `elm.input.key.W`, `elm.input.mouse.Left` |
| `elm.scene` | `spawn(name?, model?, parent?)`, `find`, `find_all`, `all`, `count`, `with(type)`; класс `Entity` (`add/remove/has(type)` для `Script`, `RigidBody`, `Collider`; `script`, `rigidbody`, `collider`) |
| `elm.models` | `cube(size?)`, `box(half)`, `plane(x, z)`, `load(path)` (.model / .gltf от `assets/`), `find(name)`; класс `Model` |
| `elm.camera` | `position`/`set_position`, `translate`, `forward`, `right`, `yaw`/`pitch`/`set_orientation`, `rotate`, `look_at`, `fov`/`set_fov` |
| `elm.physics` | `raycast(origin, direction, max_distance?, ignore?)`, `gravity`/`set_gravity`, `body_count`; классы `RigidBody`, `Collider` |

```lua
local Vector3 = elm.math.Vector3
local cube = elm.models.cube()
local e = elm.scene.spawn("Box", cube)
e.position = Vector3(0, 1, 0)                 -- локальные position/rotation/scale
e:rotate(Vector3.up(), dt)                    -- вокруг оси в пространстве родителя
print(e.world_position, e.parent, e.valid)
e:destroy()                                   -- вместе с потомками
```

Соглашения:
- векторы-столбцы, трансформ = `T * R * S`, углы в радианах;
- Эйлер — `(pitch, yaw, roll)` вокруг X, Y, Z;
- индексы матрицы `get/set(row, col)` с 1.

`Vector3` и остальные значения в Lua — изменяемые объекты: `b = a` делает алиас, а не копию,
для копии есть `a:copy()`.

Одному узлу всегда соответствует один и тот же Lua-объект `Entity`, поэтому сущности
сравниваются через `==` и годятся как ключи таблиц. После удаления узла все обращения,
кроме `valid`, бросают ошибку.

Каждый вызов `elm.models.cube()` создаёт новую модель с GPU-мешем. Создайте модель один раз
и переиспользуйте её (`elm.models.find(name) or elm.models.cube()`).

Пример: `assets/scripts/demo/spinner.lua`. Чтобы запустить его, установите `Script/MainModule = demo.spinner`.

## Узлы сцены

Скрипты ссылаются на узлы через `NodeId` (`Scene::IdOf`, `IndexOf`, `Find`), а не через
индексы. Индексы сдвигаются при удалении узлов, а id остаётся прежним. id удалённого узла
перестаёт находиться благодаря проверке поколения.

- `Scene::SetLocalTransform` + `UpdateWorldTransforms()` пересчитывают мировые трансформы
  за один проход.
- `RemoveNode` переносит детей к родителю удалённого узла и сохраняет их мировые трансформы.
- `Scene::Components<T>()` — компоненты типа `T` по `NodeId` (sparse set, `Scene/ComponentStore.hpp`):
  добавление, поиск и удаление за O(1), обход по плотному массиву. Хранилище создаётся при первом
  обращении, компоненты удаляются вместе с узлом (`RemoveNode`, `Clear`). Так хранятся
  `ScriptComponent`, `RigidBody` и `Collider`. Биндинги работают с типами по имени через
  `bindings::ComponentKind` (`script/bindings/Components.hpp`).

## Инструменты

```bash
Engine --script-selftest      # C++-проверки и assets/scripts/tests/*_test.lua, код выхода = число провалов
Engine --dump-script-api      # assets/scripts/.meta/elm.lua: аннотации для LuaLS
```

`assets/scripts/.luarc.json` подключает `.meta` к языковому серверу Lua в редакторе.
После изменения биндингов файл нужно сгенерировать заново.

## См. также
- [[Роли потоков]], [[Главный цикл]]
