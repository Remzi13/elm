#pragma once

// CommandQueue.h — точка входа для системы команд рендера.
//
// Зависимости:
//   CommandList.hpp  — обобщённый список команд (нет зависимости от команд)
//   Command.hpp      — конкретные команды + CommandVariant + using CommandList
//
// При добавлении новой команды трогайте только Command.hpp.

#include "graphics/render/Command.hpp"

namespace elm {
namespace render {

    class CommandQueue {
    public:
        // TODO: реализация очереди (submit / flush / синхронизация)
    };

} // namespace render
} // namespace elm
