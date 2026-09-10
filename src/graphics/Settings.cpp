#include "graphics/Settings.hpp"


namespace elm {

	String Settings::MakeKey(Category category, StringView path)
	{
		String result;
		result.reserve(path.size() + 16);

		switch (category)
		{
		case Category::Core:
			result = "Core/";
			break;

		case Category::Render:
			result = "Render/";
			break;

		case Category::Physics:
			result = "Physics/";
			break;
		}

		result += path;

		return result;
	}

}