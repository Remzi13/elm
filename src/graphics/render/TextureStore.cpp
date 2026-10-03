#include "graphics/render/TextureStore.hpp"

#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/Texture.h"
namespace elm::render {

void TextureStore::ReleaseData(Data& data)
{
	if (data.pTexture) {
		data.pTexture->Release();
		data.pTexture = nullptr;
	}
}

TextureStore::~TextureStore()
{
	Clear();
}

core::Handler TextureStore::Create(Diligent::IRenderDevice* renderDevice, core::Handler handler,
	const Diligent::TextureDesc& description)
{
	if (!renderDevice || !handler.IsValid())
		return {};

	Diligent::ITexture* texture = nullptr;
	renderDevice->CreateTexture(description, nullptr, &texture);
	if (!texture)
		return {};

	Insert(handler, { texture, description.Width, description.Height });
	return handler;
}

void TextureStore::Insert(core::Handler handler, Data data)
{
	if (!handler.IsValid()) {
		ReleaseData(data);
		return;
	}

	Release(handler);
	m_textures.emplace(handler, data);
}

const TextureStore::Data* TextureStore::Find(const core::Handler& handler) const
{
	const auto it = m_textures.find(handler);
	return it != m_textures.end() ? &it->second : nullptr;
}

void TextureStore::Release(const core::Handler& handler)
{
	const auto it = m_textures.find(handler);
	if (it == m_textures.end())
		return;

	ReleaseData(it->second);
	m_textures.erase(it);
}

void TextureStore::Clear()
{
	for (auto& [_, data] : m_textures)
		ReleaseData(data);
	m_textures.clear();
}

} // namespace elm::render