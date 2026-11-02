#include "core/FrameClock.hpp"

#include "core/Timer.hpp"

#include <algorithm>

namespace elm {

    namespace {

        /// How long the frames counted for the frame rate last.
        constexpr float CountingSeconds = 1.0f;

    } // namespace

    void FrameClock::Start()
    {
        m_lastTick = core::getTimeStamp();
        m_fixedAccumulator = 0.0f;
    }

    float FrameClock::Tick()
    {
        const uint64_t now = core::getTimeStamp();
        const float deltaTime = std::min(static_cast<float>(core::getMilliseconds(m_lastTick, now)) / 1000.f, MaxFrameSeconds);
        m_lastTick = now;
        CountFrame(deltaTime);
        return deltaTime;
    }

    uint32_t FrameClock::TakeFixedSteps(float deltaTime)
    {
        m_fixedAccumulator += deltaTime;
        uint32_t steps = 0;
        while (m_fixedAccumulator >= FixedStep) {
            m_fixedAccumulator -= FixedStep;
            ++steps;
        }
        return steps;
    }

    void FrameClock::CountFrame(float deltaTime)
    {
        m_countedTime += deltaTime;
        ++m_countedFrames;
        if (m_countedTime < CountingSeconds)
            return;
        m_fps = static_cast<float>(m_countedFrames) / m_countedTime;
        m_averageFrameMs = m_countedTime / static_cast<float>(m_countedFrames) * 1000.0f;
        m_countedTime = 0.0f;
        m_countedFrames = 0;
    }

} // namespace elm
