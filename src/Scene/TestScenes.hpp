#pragma once

#include "core/Std.hpp"

#include "math/Matrix.hpp"
#include "math/Primitivs.hpp"

namespace elm {

	using namespace math;

	enum class ScenePreset {
		Box,
		WallAndGrid,
		RoomsAndCorridors,
		PhysicsSandbox
	};

	struct Scene
	{

		struct Instance {
			MeshData mesh;
			Matrix4x4 worldTransform;
			Vector4 color{ 0.8f, 0.8f, 0.8f, 1.0f };
		};

		ScenePreset preset;
		Vector<Instance> occluders;
	};

	class TestScenes {
	public:
		static void BuildScene(ScenePreset preset, uint32_t targetInstanceCount, Scene& scene);

	private:
		static void BuildWallAndGrid(uint32_t count, Scene& scene);
		static void BuildRooms(uint32_t count, Scene& scene);
		static void BuildPhysicsSandbox(uint32_t count, Scene& scene);
		static void BuildBox(uint32_t count, Scene& scene);
	};

} // namespace Engine
