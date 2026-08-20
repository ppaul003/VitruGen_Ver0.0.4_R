#ifndef VITRUGEN_PROJECT_ASSET_REPOSITORY_H
#define VITRUGEN_PROJECT_ASSET_REPOSITORY_H

#include "StaticParticleAsset.h"

#include <vector>

namespace vitru {

class ProjectAssetRepository {
public:
	AssetId addStaticParticle(StaticParticleAsset asset);
	bool replaceStaticParticle(AssetId id, StaticParticleAsset asset);
	bool removeStaticParticle(AssetId id);
	StaticParticleAsset* findStaticParticle(AssetId id);
	const StaticParticleAsset* findStaticParticle(AssetId id) const;
	void clear();
	bool setActiveStaticParticle(AssetId id);
	AssetId activeStaticParticleId() const { return m_activeAssetId; }
	StaticParticleAsset* activeStaticParticle();
	const StaticParticleAsset* activeStaticParticle() const;
	const std::vector<StaticParticleAsset>& staticParticles() const { return m_assets; }

private:
	AssetId allocateId();
	AssetId m_nextId = 1;
	AssetId m_activeAssetId = INVALID_ASSET_ID;
	std::vector<StaticParticleAsset> m_assets;
};

} // namespace vitru

#endif
