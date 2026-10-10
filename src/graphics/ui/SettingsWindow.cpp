#include "graphics/ui/SettingsWindow.hpp"

#include "graphics/culling/OcclusionCullingSystem.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>

namespace elm {
	namespace {

		constexpr std::array<const char*, 3> VisualModeChoices{
			"Visible Only",
			"Highlight Culled",
			"Occluders Only"
		};

		struct SettingsGroup {
			String name;
			Vector<SettingsGroup> groups;
			Vector<Settings::Entry> entries;
		};

		auto GetOrCreateGroup(SettingsGroup& parent, StringView name) -> SettingsGroup&
		{
			const auto group = std::find_if(parent.groups.begin(), parent.groups.end(),
				[name](const SettingsGroup& candidate) {
					return StringView(candidate.name.data(), candidate.name.size()) == name;
				});
			if (group != parent.groups.end())
				return *group;

			parent.groups.push_back(SettingsGroup{ String(name.begin(), name.end()), {}, {} });
			return parent.groups.back();
		}

		auto BuildGroups(Vector<Settings::Entry> entries) -> SettingsGroup
		{
			SettingsGroup root;
			for (auto& entry : entries) {
				SettingsGroup* group = &root;
				const StringView path(entry.path.data(), entry.path.size());
				size_t segmentStart = 0;
				size_t separator = path.find('/');
				while (separator != StringView::npos) {
					if (separator > segmentStart)
						group = &GetOrCreateGroup(*group, path.substr(segmentStart, separator - segmentStart));
					segmentStart = separator + 1;
					separator = path.find('/', segmentStart);
				}

				group->entries.push_back(std::move(entry));
			}
			return root;
		}

		void RenderSetting(const Settings::Entry& entry, Settings& settings)
		{
			ImGui::PushID(static_cast<int>(entry.category));
			ImGui::PushID(entry.path.c_str());
			ImGui::TextUnformatted(entry.label.c_str());
			ImGui::SameLine();

			if (const auto* boolValue = std::get_if<bool>(&entry.value)) {
				bool edited = *boolValue;
				if (ImGui::Checkbox("##value", &edited))
					settings.Set(entry.category, StringView(entry.path.data(), entry.path.size()), edited);
			}
			else if (const auto* floatValue = std::get_if<float>(&entry.value)) {
				float edited = *floatValue;
				if (ImGui::DragFloat("##value", &edited, 0.01f, 0.0f, 0.0f, "%.4f"))
					settings.Set(entry.category, StringView(entry.path.data(), entry.path.size()), edited);
			}
			else if (const auto* integerValue = std::get_if<uint32_t>(&entry.value)) {
				const bool isVisualMode = entry.category == Settings::Category::Render &&
					StringView(entry.path.data(), entry.path.size()) ==
						OcclusionCullingSystem::VisualModeSetting;
				if (isVisualMode) {
					const bool validSelection = *integerValue < VisualModeChoices.size();
					const char* preview = validSelection ? VisualModeChoices[*integerValue] : "<invalid>";
					if (ImGui::BeginCombo("##value", preview)) {
						for (size_t index = 0; index < VisualModeChoices.size(); ++index) {
							const bool selected = *integerValue == index;
							if (ImGui::Selectable(VisualModeChoices[index], selected))
								settings.Set(entry.category, StringView(entry.path.data(), entry.path.size()),
									static_cast<uint32_t>(index));
							if (selected)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
				}
				else {
					uint32_t edited = *integerValue;
					if (ImGui::InputScalar("##value", ImGuiDataType_U32, &edited))
						settings.Set(entry.category, StringView(entry.path.data(), entry.path.size()), edited);
				}
			}
			else if (const auto* stringValue = std::get_if<String>(&entry.value)) {
				Vector<char> buffer((std::max)(stringValue->size() + 1, size_t{ 128 }), '\0');
				std::copy_n(stringValue->data(), stringValue->size(), buffer.data());
				if (ImGui::InputText("##value", buffer.data(), buffer.size()))
					settings.Set(entry.category, StringView(entry.path.data(), entry.path.size()),
						String(buffer.data()));
			}

			ImGui::PopID();
			ImGui::PopID();
		}

		void RenderGroup(const SettingsGroup& group, Settings& settings)
		{
			ImGui::PushID(group.name.c_str());
			if (ImGui::CollapsingHeader(group.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
				for (const auto& entry : group.entries)
					RenderSetting(entry, settings);
				for (const auto& child : group.groups)
					RenderGroup(child, settings);
			}
			ImGui::PopID();
		}

		void RenderSettingsGroup(const SettingsGroup& group, Settings& settings)
		{
			for (const auto& entry : group.entries)
				RenderSetting(entry, settings);
			for (const auto& child : group.groups)
				RenderGroup(child, settings);
		}

	}

	void SettingsWindow::Render(ImGuiWindowContext&)
	{
		if (!m_visible)
			return;

		ImGui::SetNextWindowSize(ImVec2(420.0f, 520.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(GetName().data(), &m_visible)) {
			ImGui::End();
			return;
		}

		const auto groups = BuildGroups(m_settings.GetEntries());
		RenderSettingsGroup(groups, m_settings);

		ImGui::Separator();
		if (ImGui::Button("Save .settings")) {
			auto result = m_settings.Save(Settings::DefaultFilePath());
			m_statusIsError = !result;
			m_status = result ? String("Settings saved.") : result.error().message;
		}
		ImGui::SameLine();
		if (ImGui::Button("Reload .settings")) {
			auto result = m_settings.Load(Settings::DefaultFilePath());
			m_statusIsError = !result;
			m_status = result ? String("Settings loaded.") : result.error().message;
		}
		if (!m_status.empty()) {
			if (m_statusIsError)
				ImGui::TextWrapped("%s", m_status.c_str());
			else
				ImGui::TextDisabled("%s", m_status.c_str());
		}

		ImGui::End();
	}

} // namespace elm
