#pragma once

#include "core/Handler.hpp"

namespace elm {

struct NodeTag;
/// Stable reference to a scene node: survives removal and reordering of other nodes,
/// a removed node's id stops resolving (generation check).
using NodeId = core::Handle<NodeTag>;

} // namespace elm
