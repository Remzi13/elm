#pragma once

#include <cstdint>

namespace elm {

    /// The time of a program's frames: the step of each frame, the fixed physics steps it makes, frames per
    /// second for the profiler. Owner: the program (Game, Editor), thread: update.
    class FrameClock {
    public:
        static constexpr float FixedStep = 1.0f / 60.0f;
        /// Longer frames are cut to this: physics does not spiral into ever more steps.
        static constexpr float MaxFrameSeconds = 0.25f;

        /// From now: the first Tick measures the time since.
        void Start();
        /// The time since the last Tick (or Start), at most MaxFrameSeconds.
        [[nodiscard]] float Tick();
        /// How many fixed steps of FixedStep the frame's time makes; the rest waits for the next frame.
        [[nodiscard]] uint32_t TakeFixedSteps(float deltaTime);

        /// Averaged over about a second of frames.
        [[nodiscard]] float GetFps() const noexcept { return m_fps; }
        [[nodiscard]] float GetAverageFrameMs() const noexcept { return m_averageFrameMs; }

    private:
        void CountFrame(float deltaTime);

        /// core::getTimeStamp() of the last Tick.
        uint64_t m_lastTick { 0 };
        /// Time not yet taken by fixed steps.
        float m_fixedAccumulator { 0.0f };
        float m_countedTime { 0.0f };
        uint32_t m_countedFrames { 0 };
        float m_fps { 0.0f };
        float m_averageFrameMs { 0.0f };
    };

} // namespace elm
