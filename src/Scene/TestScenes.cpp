#include "Scene/TestScenes.hpp"
#include <cmath>

namespace elm {

	namespace {

		OccluderInstance MakeWall(float width, float height, float thickness, const Vector3& position) {
			OccluderInstance wall;
			wall.mesh = GeometryPrimitives::CreateWall(1.0f, 1.0f, 1.0f);
			wall.worldTransform = Matrix4x4::Scaling(Vector3{ width, height, thickness }) *
				Matrix4x4::Translation(position);
			return wall;
		}

	} // namespace

	void TestScenes::BuildScene(ScenePreset preset,
		uint32_t targetInstanceCount,
		Vector<OccluderInstance>& outOccluders,
		Vector<OccludeeInstance>& outOccludees) {
		outOccluders.clear();
		outOccludees.clear();

		switch (preset) {
		case ScenePreset::WallAndGrid:
			BuildWallAndGrid(targetInstanceCount, outOccluders, outOccludees);
			break;
		case ScenePreset::RoomsAndCorridors:
			BuildRooms(targetInstanceCount, outOccluders, outOccludees);
			break;
		case ScenePreset::PhysicsSandbox:
			BuildPhysicsSandbox(targetInstanceCount, outOccluders, outOccludees);
			break;
		}
	}

	void TestScenes::BuildWallAndGrid(uint32_t count,
		Vector<OccluderInstance>& outOccluders,
		Vector<OccludeeInstance>& outOccludees) {
		// 1. Occluders: Two massive walls at Z = 0 with a doorway in the center
		// Left Wall: width 18, height 9, thickness 1.5, center at (-10.5, 4.5, 0)
			{
				outOccluders.push_back(MakeWall(18.0f, 9.0f, 1.5f, Vector3{ -10.5f, 4.5f, 0.0f }));
			}
			// Right Wall: width 18, height 9, thickness 1.5, center at (+10.5, 4.5, 0)
			{
				outOccluders.push_back(MakeWall(18.0f, 9.0f, 1.5f, Vector3{ 10.5f, 4.5f, 0.0f }));
			}
			// Middle Lintle (above doorway): width 3, height 3, center at (0, 7.5, 0)
			{
				outOccluders.push_back(MakeWall(3.0f, 3.0f, 1.5f, Vector3{ 0.0f, 7.5f, 0.0f }));
			}
			// Second layer occluder wall further back: center at (0, 4.0, 18.0)
			{
				outOccluders.push_back(MakeWall(12.0f, 8.0f, 1.0f, Vector3{ 0.0f, 4.0f, 18.0f }));
			}

			// 2. Occludees: Distributed in rows/columns behind the walls (Z from 3 to 45)
			outOccludees.reserve(count);
			const uint32_t cols = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(count) * 1.5f)));
			const uint32_t rows = static_cast<uint32_t>(std::ceil(static_cast<float>(count) / static_cast<float>(cols)));

			const float startX = -20.0f;
			const float stepX = 40.0f / static_cast<float>(cols > 1 ? cols - 1 : 1);
			const float startZ = 4.0f;
			const float stepZ = 40.0f / static_cast<float>(rows > 1 ? rows - 1 : 1);

			const MeshData cubeMesh = GeometryPrimitives::CreateCube(0.8f);

			uint32_t id = 0;
			for (uint32_t r = 0; r < rows && id < count; ++r) {
				for (uint32_t c = 0; c < cols && id < count; ++c) {
					const float x = startX + static_cast<float>(c) * stepX;
					const float z = startZ + static_cast<float>(r) * stepZ;
					const float y = 0.5f + static_cast<float>((id % 4)) * 0.9f;

					OccludeeInstance inst;
					inst.id = id++;
					inst.localBounds = cubeMesh.localBounds;
					inst.worldTransform = Matrix4x4::Translation(Vector3{ x, y, z });

					// Color gradient across grid
					const float tX = static_cast<float>(c) / static_cast<float>(cols);
					const float tZ = static_cast<float>(r) / static_cast<float>(rows);
					inst.color = Vector4{ 0.2f + 0.7f * tX, 0.4f + 0.5f * (1.0f - tZ), 0.3f + 0.6f * tZ, 1.0f };

					outOccludees.push_back(inst);
				}
			}
	}

	void TestScenes::BuildRooms(uint32_t count,
		Vector<OccluderInstance>& outOccluders,
		Vector<OccludeeInstance>& outOccludees) {
		// 4 room layout with dividing walls
		const float roomSize = 16.0f;
		const float wallH = 6.0f;
		const float wallT = 1.0f;

		// Center dividing wall along X (Z=0), with doorway at X=0
		{
			outOccluders.push_back(MakeWall(14.0f, wallH, wallT, Vector3{ -8.5f, wallH * 0.5f, 0.0f }));
			outOccluders.push_back(MakeWall(14.0f, wallH, wallT, Vector3{ 8.5f, wallH * 0.5f, 0.0f }));
		}

		// Center dividing wall along Z (X=0), with doorway at Z=0
		{
			outOccluders.push_back(MakeWall(wallT, wallH, 14.0f, Vector3{ 0.0f, wallH * 0.5f, -8.5f }));
			outOccluders.push_back(MakeWall(wallT, wallH, 14.0f, Vector3{ 0.0f, wallH * 0.5f, 8.5f }));
		}

		// Outer boundary walls
		{
			outOccluders.push_back(MakeWall(roomSize * 2.0f, wallH, wallT, Vector3{ 0.0f, wallH * 0.5f, roomSize }));
			outOccluders.push_back(MakeWall(roomSize * 2.0f, wallH, wallT, Vector3{ 0.0f, wallH * 0.5f, -roomSize }));
			outOccluders.push_back(MakeWall(wallT, wallH, roomSize * 2.0f, Vector3{ roomSize, wallH * 0.5f, 0.0f }));
			outOccluders.push_back(MakeWall(wallT, wallH, roomSize * 2.0f, Vector3{ -roomSize, wallH * 0.5f, 0.0f }));
		}

		// Scatter objects across 4 rooms
		const MeshData cubeMesh = GeometryPrimitives::CreateCube(0.7f);
		outOccludees.reserve(count);

		for (uint32_t i = 0; i < count; ++i) {
			const int roomIdx = static_cast<int>(i % 4);
			const float rx = ((roomIdx % 2 == 0) ? -1.0f : 1.0f) * 8.0f;
			const float rz = ((roomIdx / 2 == 0) ? -1.0f : 1.0f) * 8.0f;

			const float angle = static_cast<float>(i) * 0.4f;
			const float radius = 1.5f + static_cast<float>(i % 8) * 0.5f;
			const float x = rx + std::cos(angle) * radius;
			const float z = rz + std::sin(angle) * radius;
			const float y = 0.5f + static_cast<float>(i % 3) * 0.8f;

			OccludeeInstance inst;
			inst.id = i;
			inst.localBounds = cubeMesh.localBounds;
			inst.worldTransform = Matrix4x4::Translation(Vector3{ x, y, z });

			if (roomIdx == 0) inst.color = Vector4{ 0.9f, 0.3f, 0.3f, 1.0f };
			else if (roomIdx == 1) inst.color = Vector4{ 0.3f, 0.9f, 0.3f, 1.0f };
			else if (roomIdx == 2) inst.color = Vector4{ 0.3f, 0.3f, 0.9f, 1.0f };
			else inst.color = Vector4{ 0.9f, 0.8f, 0.2f, 1.0f };

			outOccludees.push_back(inst);
		}
	}

	void TestScenes::BuildPhysicsSandbox(uint32_t count,
		Vector<OccluderInstance>& outOccluders,
		Vector<OccludeeInstance>& outOccludees) {
		// A center barrier wall
			{
				outOccluders.push_back(MakeWall(16.0f, 6.0f, 1.5f, Vector3{ 0.0f, 3.0f, 0.0f }));
			}

			const MeshData cubeMesh = GeometryPrimitives::CreateCube(0.9f);
			outOccludees.reserve(count);

			// Stacks of boxes behind the barrier, and visible boxes in front
			for (uint32_t i = 0; i < count; ++i) {
				const bool behindWall = (i % 3) != 0; // 2/3 behind, 1/3 in front
				const float z = behindWall ? (4.0f + static_cast<float>(i % 10) * 1.5f) : (-5.0f - static_cast<float>(i % 5) * 1.5f);
				const float x = -6.0f + static_cast<float>(i % 12) * 1.0f;
				const float y = 0.5f + static_cast<float>(i / 12) * 1.0f;

				OccludeeInstance inst;
				inst.id = i;
				inst.localBounds = cubeMesh.localBounds;
				inst.worldTransform = Matrix4x4::Translation(Vector3{ x, y, z });
				inst.color = behindWall ? Vector4{ 0.2f, 0.7f, 0.9f, 1.0f } : Vector4{ 0.9f, 0.6f, 0.2f, 1.0f };

				outOccludees.push_back(inst);
			}
	}

} // namespace Engine
