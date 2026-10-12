#include "graphics/ModelImport.hpp"

namespace elm::graphics {

namespace {

    [[nodiscard]] String NodeName(const Scene& scene, size_t nodeIndex)
    {
        const auto& name = scene.nodes[nodeIndex].name;
        return name.empty() ? "Node " + String(std::to_string(nodeIndex)) : name;
    }

    /// Parent of a node, or NoNode for roots and nodes whose parent does not precede them.
    [[nodiscard]] size_t ParentOf(const Scene& scene, size_t nodeIndex)
    {
        const size_t parent = scene.nodes[nodeIndex].parentIndex;
        return parent < nodeIndex ? parent : Scene::NoNode;
    }

    /// Appends the submeshes of the node's mesh with transform baked into the geometry.
    void AppendNodeMesh(Model& target, const SceneAsset& asset, size_t nodeIndex, const Matrix4x4& transform)
    {
        const auto& source = asset.models[asset.nodeModels[nodeIndex]];
        const bool identity = transform.m == Matrix4x4::Identity().m;
        const String nodeName = asset.scene.nodes[nodeIndex].name;
        for (const auto& submesh : source.submeshes) {
            if (!submesh.data)
                continue;
            String name = nodeName;
            if (!submesh.name.empty() && submesh.name != nodeName) {
                if (!name.empty())
                    name += "/";
                name += submesh.name;
            }
            if (name.empty())
                name = "Submesh " + String(std::to_string(target.submeshes.size()));
            auto data = identity ? submesh.data : MakeShared<const MeshData>(TransformMeshData(*submesh.data, transform));
            target.AddSubmesh(std::move(data), submesh.color, std::move(name));
        }
    }

} // namespace

Vector<uint32_t> ComputeNodeDepths(const Scene& scene)
{
    // Parents precede their children (glTF loading is depth-first)
    Vector<uint32_t> depths(scene.nodes.size(), 0);
    for (size_t nodeIndex = 0; nodeIndex < scene.nodes.size(); ++nodeIndex) {
        const size_t parent = ParentOf(scene, nodeIndex);
        depths[nodeIndex] = parent == Scene::NoNode ? 0 : depths[parent] + 1;
    }
    return depths;
}

Vector<ImportedModel> BuildSceneImport(const SceneAsset& asset, const Vector<bool>& selected,
    const SceneImportOptions& options)
{
    const auto& scene = asset.scene;
    const auto hasMesh = [&](size_t nodeIndex) {
        return nodeIndex < selected.size() && selected[nodeIndex] && nodeIndex < asset.nodeModels.size()
            && asset.nodeModels[nodeIndex] < asset.models.size();
    };

    Vector<ImportedModel> result;
    switch (options.mode) {
    case SceneImportMode::MergeAll: {
        ImportedModel merged { Model(options.name), {} };
        for (size_t nodeIndex = 0; nodeIndex < scene.nodes.size(); ++nodeIndex) {
            if (hasMesh(nodeIndex))
                AppendNodeMesh(merged.model, asset, nodeIndex, scene.nodes[nodeIndex].worldTransform);
        }
        if (!merged.model.submeshes.empty()) {
            merged.instanceNodes.push_back(Scene::NoNode);
            result.push_back(std::move(merged));
        }
        break;
    }

    case SceneImportMode::ModelPerMesh: {
        Vector<size_t> slots(asset.models.size(), SceneAsset::NoModel);
        for (size_t nodeIndex = 0; nodeIndex < scene.nodes.size(); ++nodeIndex) {
            if (!hasMesh(nodeIndex))
                continue;
            const size_t meshIndex = asset.nodeModels[nodeIndex];
            if (slots[meshIndex] == SceneAsset::NoModel) {
                slots[meshIndex] = result.size();
                ImportedModel imported { asset.models[meshIndex], {} };
                if (imported.model.name.empty())
                    imported.model.name = "Mesh " + String(std::to_string(meshIndex));
                result.push_back(std::move(imported));
            }
            result[slots[meshIndex]].instanceNodes.push_back(nodeIndex);
        }
        break;
    }

    case SceneImportMode::ModelPerGroup: {
        // A node belongs to its ancestor at groupDepth; shallower nodes are groups of their own mesh only
        const auto depths = ComputeNodeDepths(scene);
        Vector<size_t> groups(scene.nodes.size(), Scene::NoNode);
        Vector<Matrix4x4> toGroup(scene.nodes.size(), Matrix4x4::Identity());
        Vector<size_t> slots(scene.nodes.size(), SceneAsset::NoModel);
        for (size_t nodeIndex = 0; nodeIndex < scene.nodes.size(); ++nodeIndex) {
            const size_t parent = ParentOf(scene, nodeIndex);
            if (depths[nodeIndex] <= options.groupDepth || parent == Scene::NoNode) {
                groups[nodeIndex] = nodeIndex;
            } else {
                groups[nodeIndex] = groups[parent];
                toGroup[nodeIndex] = toGroup[parent] * scene.nodes[nodeIndex].localTransform;
            }
            if (!hasMesh(nodeIndex))
                continue;

            const size_t group = groups[nodeIndex];
            if (slots[group] == SceneAsset::NoModel) {
                slots[group] = result.size();
                result.push_back({ Model(NodeName(scene, group)), { group } });
            }
            AppendNodeMesh(result[slots[group]].model, asset, nodeIndex, toGroup[nodeIndex]);
        }
        break;
    }
    }
    return result;
}

} // namespace elm::graphics
