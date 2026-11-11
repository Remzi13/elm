#pragma once

#include "core/FileSystem.hpp"
#include "core/Std.hpp"

#include <cstdint>

namespace elm {

    class ModelCatalog;
    class Scene;
    class SceneEdits;
    struct Selection;

    namespace script {
        struct PlayRequests;
    }

    /// The last thing that happened to the scene, shown in the main menu bar (SceneMenu).
    struct SceneStatus {
        enum class Kind : uint8_t {
            Info,
            Success,
            Failure
        };
        Kind kind { Kind::Info };
        String text;
    };

    /// What a SceneDocument works with; it all outlives the document.
    struct SceneDocumentDeps {
        Scene& scene;
        ModelCatalog& models;
        /// Knows whether the scene changed since it was saved; emptied when the scene is replaced.
        SceneEdits& edits;
        /// Replaced scenes leave nothing selected.
        Selection& selection;
        /// In Play the scene is the running game: it is not saved or replaced.
        const script::PlayRequests& play;
    };

    /// The scene the editor has open as a file: which file it is, whether it has unsaved changes, and New, Open,
    /// Save, Save As, glTF import and export. Knows nothing of UI: asking before unsaved changes go is SceneMenu's.
    /// Stop brings the edited scene back, so in Play nothing here saves or replaces it: the file would no longer
    /// name the scene, and the next Save would overwrite another scene's file.
    /// Owner: Editor, while a project is open. Thread: update, outside of the windows that read the scene.
    class SceneDocument {
    public:
        /// assets: what scene files reference models relative to. file: the scene the editor opened with the
        /// project; empty: a name no scene has yet, so that Save does not overwrite one the user did not open.
        SceneDocument(const SceneDocumentDeps& deps, Path assets, Path scenes, const Path& file);

        SceneDocument(const SceneDocument&) = delete;
        SceneDocument& operator=(const SceneDocument&) = delete;
        SceneDocument(SceneDocument&&) = delete;
        SceneDocument& operator=(SceneDocument&&) = delete;

        [[nodiscard]] const Path& File() const noexcept { return m_file; }
        /// The file relative to the scenes folder, in full when it lies outside of it.
        [[nodiscard]] String ShownName() const;
        [[nodiscard]] bool IsModified() const noexcept;
        [[nodiscard]] bool IsPlaying() const;
        [[nodiscard]] const Path& AssetsFolder() const noexcept { return m_assets; }
        [[nodiscard]] const Path& ScenesFolder() const noexcept { return m_scenes; }

        /// Empties the scene under a file name no scene has yet.
        void New();
        /// Replaces the scene with a scene file. When it cannot be read the scene stays.
        void Open(const Path& file);
        /// False when it did not save: in Play or on an error, the status says why.
        bool Save();
        /// Saves to another file, which becomes the scene's file.
        bool SaveAs(const Path& file);
        /// Adds a glTF scene to this one: the original goes to the project's source folder, the models to its
        /// models folder.
        void ImportGltf(const Path& file);
        void ExportGltf(const Path& file);

        [[nodiscard]] const SceneStatus& Status() const noexcept { return m_status; }
        /// Also for what the windows do to the scene: moved nodes, failed edits.
        void Report(SceneStatus::Kind kind, String text);

    private:
        /// new.scene, new_1.scene, ...: the first name in the scenes folder no file has.
        [[nodiscard]] Path UnusedFile() const;
        [[nodiscard]] bool RefuseInPlay();

        SceneDocumentDeps m_deps;
        Path m_assets;
        Path m_scenes;
        Path m_file;
        SceneStatus m_status;
    };

} // namespace elm
