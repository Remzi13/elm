#pragma once

#include "graphics/debug/DebugDraw.hpp"
#include "graphics/debug/IDebugVisualizer.hpp"

namespace elm {
struct PhysicsFrameLog;
}

namespace elm::graphics {

/// Turns what the physics system did during the frame into debug primitives: the ray casts with
/// their hits, and the contacts that began. Reads the frame log only, which the physics system
/// records and clears at the start of a frame; the log must outlive the visualizer. Update thread.
class PhysicsVisualizer final : public IDebugVisualizer {
public:
    /// Contact markers stay this long, so a contact of a single step can be seen.
    static constexpr float ContactLifetime = 0.5f;

    PhysicsVisualizer(const PhysicsFrameLog& log, DebugDraw& debugDraw);

    void Draw(DebugDraw& debugDraw) override;

private:
    /// A hit is a line to the point with its normal, a miss a line over the whole distance. Frame primitives.
    void DrawRayQueries(DebugDraw& debugDraw) const;
    /// A marker at every contact, kept for ContactLifetime: the log has each contact for one frame only.
    void DrawContacts(DebugDraw& debugDraw) const;

    const PhysicsFrameLog& m_log;
    DebugChannel m_raysChannel;
    DebugChannel m_contactsChannel;
};

} // namespace elm::graphics
