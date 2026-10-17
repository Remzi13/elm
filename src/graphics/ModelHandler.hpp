#pragma once

#include "core/Handler.hpp"

namespace elm::graphics {

    struct ModelTag;
    /// A model of a ModelManager (historical name, not to be confused with core::Handle).
    /// Only the handle type: code that keeps models by handle does not need the model itself.
    using ModelHandler = core::Handle<ModelTag>;

} // namespace elm::graphics
