#pragma once

#include "graphics/culling/OcclusionCullingSystem.hpp"
#include <string>

namespace elm {

	enum class ScenePreset {
		WallAndGrid,
		RoomsAndCorridors,
		PhysicsSandbox
	};

	class TestScenes {
	public:
		static void BuildScene(ScenePreset preset,
			uint32_t targetInstanceCount,
			Vector<OccluderInstance>& outOccluders,
			Vector<OccludeeInstance>& outOccludees);

	private:
		static void BuildWallAndGrid(uint32_t count,
			Vector<OccluderInstance>& outOccluders,
			Vector<OccludeeInstance>& outOccludees);

		static void BuildRooms(uint32_t count,
			Vector<OccluderInstance>& outOccluders,
			Vector<OccludeeInstance>& outOccludees);

		static void BuildPhysicsSandbox(uint32_t count,
			Vector<OccluderInstance>& outOccluders,
			Vector<OccludeeInstance>& outOccludees);
	};

} // namespace Engine
