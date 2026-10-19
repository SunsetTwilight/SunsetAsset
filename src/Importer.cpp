// ============================================================
//  src/Importer.cpp
// ============================================================
#include "sunset/asset/Importer.h"

#include <algorithm>
#include <cctype>

namespace sunset::asset
{
namespace
{
std::string lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(c < 0x80 ? std::tolower(c) : c); });
	return s;
}
} // namespace

bool ImporterRegistry::add(std::unique_ptr<Importer> importer)
{
	if (!importer || importer->name().empty())
		return false;
	for (const auto& e : m_importers)
	{
		if (e->name() == importer->name())
			return false;
		for (const auto& a : e->extensions())
			for (const auto& b : importer->extensions())
				if (lower(a) == lower(b))
					return false;
	}
	m_importers.push_back(std::move(importer));
	return true;
}

const Importer* ImporterRegistry::byName(std::string_view name) const
{
	for (const auto& e : m_importers)
		if (e->name() == name)
			return e.get();
	return nullptr;
}

const Importer* ImporterRegistry::forFile(const std::filesystem::path& file) const
{
	const std::string name = lower(genericUtf8(file.filename()));
	const Importer* best = nullptr;
	size_t bestLen = 0;
	for (const auto& e : m_importers)
		for (const auto& ext : e->extensions())
		{
			const std::string x = lower(ext);
			if (x.size() > bestLen && name.size() > x.size() && name.compare(name.size() - x.size(), x.size(), x) == 0)
			{
				best    = e.get();
				bestLen = x.size();
			}
		}
	return best;
}

std::vector<const Importer*> ImporterRegistry::all() const
{
	std::vector<const Importer*> out;
	for (const auto& e : m_importers)
		out.push_back(e.get());
	return out;
}

Json ImporterRegistry::mergeSettings(const Json& defaults, const Json& settings)
{
	if (!defaults.is_object() || !settings.is_object())
		return settings.is_null() ? defaults : settings;
	Json out = defaults;
	for (const auto& [k, v] : settings.items())
		out[k] = out.contains(k) ? mergeSettings(out[k], v) : v;
	return out;
}
} // namespace sunset::asset
