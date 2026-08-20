#include "ProjectAssetRepository.h"

#include <algorithm>

namespace vitru {

AssetId ProjectAssetRepository::allocateId() {
	while (findStaticParticle(m_nextId)) ++m_nextId;
	return m_nextId++;
}

AssetId ProjectAssetRepository::addStaticParticle(StaticParticleAsset asset) {
	if (asset.id == INVALID_ASSET_ID || findStaticParticle(asset.id)) asset.id = allocateId();
	else if (asset.id >= m_nextId) m_nextId = asset.id + 1;
	const AssetId id = asset.id;
	m_assets.push_back(std::move(asset));
	return id;
}

bool ProjectAssetRepository::replaceStaticParticle(AssetId id, StaticParticleAsset asset) {
	StaticParticleAsset* existing = findStaticParticle(id);
	if (!existing) return false;
	asset.id = id;
	*existing = std::move(asset);
	return true;
}

bool ProjectAssetRepository::removeStaticParticle(AssetId id) {
	const auto found = std::find_if(m_assets.begin(), m_assets.end(),
		[id](const StaticParticleAsset& asset) { return asset.id == id; });
	if (found == m_assets.end()) return false;
	m_assets.erase(found);
	if (m_activeAssetId == id) m_activeAssetId = INVALID_ASSET_ID;
	return true;
}

StaticParticleAsset* ProjectAssetRepository::findStaticParticle(AssetId id) {
	for (StaticParticleAsset& asset : m_assets) if (asset.id == id) return &asset;
	return nullptr;
}
const StaticParticleAsset* ProjectAssetRepository::findStaticParticle(AssetId id) const {
	for (const StaticParticleAsset& asset : m_assets) if (asset.id == id) return &asset;
	return nullptr;
}

void ProjectAssetRepository::clear() {
	m_assets.clear();
	m_nextId = 1;
	m_activeAssetId = INVALID_ASSET_ID;
}

bool ProjectAssetRepository::setActiveStaticParticle(AssetId id) {
	if (id != INVALID_ASSET_ID && !findStaticParticle(id)) return false;
	m_activeAssetId = id;
	return true;
}
StaticParticleAsset* ProjectAssetRepository::activeStaticParticle() {
	return findStaticParticle(m_activeAssetId);
}
const StaticParticleAsset* ProjectAssetRepository::activeStaticParticle() const {
	return findStaticParticle(m_activeAssetId);
}

} // namespace vitru
