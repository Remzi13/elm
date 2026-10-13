---
tags: [script, lua]
aliases: [ScriptSystem, Lua, lua::Class, lua::Module]
---
# Скрипты на Lua

Файлы: `script/ScriptSystem.hpp`, `script/lua/*`. Единственная зависимость — Lua 5.4.7
(`third_party/CMakeLists.txt`). Обёртка над C API своя, sol2 и подобных библиотек нет.

Lua собирается **как C++**. Поэтому `lua_error` бросает исключение, а не делает `longjmp`,
и ошибка в биндинге вызывает деструкторы C++-объектов. Заголовки подключаются через
`script/lua/Lua.hpp`, никогда через `lua.hpp`: его `extern "C"` не совпадёт с линковкой.

## ScriptSystem

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
function main.on_update(dt) end        -- после input, до пересчёта трансформов и culling
function main.on_fixed_update(dt) end  -- после шага физики
function main.on_shutdown() end
return main
```

Главный модуль стартует в начале `EngineApp::Run`, после того как загружена начальная сцена
(`--scene`).

### main.lua — точка входа

`assets/scripts/main.lua` описывает сцену функцией `main.build_scene()`. Её вызывают `on_start`
и `on_reload`, поэтому каждое сохранение файла собирает сцену заново по новому коду.
Кнопка «Rebuild scene» в окне Scripts и команда `/scene` в его консоли вызывают
`build_scene()` без перезагрузки модуля (`ScriptSystem::CallMain`).

`BUILD_SCENE = false` в начале файла оставляет сцену из `--scene` или окна Scene Hierarchy. Если хук упал, ошибка с traceback уходит в лог, и хуки главного модуля
не вызываются до перезагрузки модуля или перезапуска скриптов.

## Hot reload

Настройка `Script/HotReload` (по умолчанию включена). Файлы загруженных модулей
проверяются четыре раза в секунду. Изменение применяется, когда два опроса подряд видят
одно и то же время файла: так редактор успевает дописать файл. Перезагрузка выполняется
в начале `ScriptSystem::Update`, вне хуков.

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

`script/ui/ScriptWindow.*`, пункт «Scripts» в меню Windows.

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
| `elm.engine` | `load_test_scene(preset, instances?)`, `clear_scene()`, `save_scene(file)`, `load_scene(file)` (файлы относительно `assets/scenes`) |
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

Регистрируются в `script/bindings/*`. Системы движка приходят через `ScriptSystem::SetEngineContext`.
Если какой-то системы нет (инструменты, самотесты), её функции бросают ошибку «not available here».

| Модуль | Что даёт |
|---|---|
| `elm.log` | `info`, `warn`, `error` (и `print`) |
| `elm.time` | `delta()`, `fixed_delta()`, `now()`, `frame()` |
| `elm.settings` | `get("Script/MainModule")`, `set(key, value)`: тип значения как при регистрации |
| `elm.math` | `Vector3`, `Vector4`, `Quaternion`, `Matrix4x4`, `lerp`, `clamp`, `radians`, `degrees`, `pi` |
| `elm.input` | `is_down/is_pressed/is_released(key)`, `is_mouse_*`, `mouse_position()`, `mouse_delta()`, `axis(name)`; константы `elm.input.key.W`, `elm.input.mouse.Left` |
| `elm.scene` | `spawn(name?, model?, parent?)`, `find`, `find_all`, `all`, `count`; класс `Entity` |
| `elm.models` | `cube(size?)`, `box(half)`, `plane(x, z)`, `load(path)` (.model / .gltf от `assets/`), `find(name)`; класс `Model` |
| `elm.camera` | `position`/`set_position`, `translate`, `forward`, `right`, `yaw`/`pitch`/`set_orientation`, `rotate`, `look_at`, `fov`/`set_fov` |

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

## Инструменты

```bash
Engine --script-selftest      # C++-проверки и assets/scripts/tests/*_test.lua, код выхода = число провалов
Engine --dump-script-api      # assets/scripts/.meta/elm.lua: аннотации для LuaLS
```

`assets/scripts/.luarc.json` подключает `.meta` к языковому серверу Lua в редакторе.
После изменения биндингов файл нужно сгенерировать заново.

## См. также
- [[Роли потоков]], [[Главный цикл]]
