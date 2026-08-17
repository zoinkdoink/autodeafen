#pragma once

#include "core/resolve.hpp"

#include <Geode/Result.hpp>

#include <map>
#include <optional>
#include <string>

class GJGameLevel;

namespace autodeafen {

// Consolidated level identity: "o:<id>" for anything with an online ID
// (downloaded, own uploads, main, daily/weekly), "l:<name>" for local
// created-tab levels, which have m_levelID == 0 and no stable built-in GUID.
std::string levelKeyFor(GJGameLevel* level);

// Per-level configs, persisted as <mod save dir>/levels.json. Re-read on every
// level enter so the file can be hand-edited while the game runs (the config
// UI arrives in a later milestone).
class ConfigStore {
public:
    static ConfigStore& get();

    // Reload from disk. Called when entering a level; cheap at realistic sizes.
    void refresh();

    std::optional<LevelConfig> levelConfig(std::string const& key);

    // Mutation API for the future in-game UI.
    void setLevelConfig(std::string const& key, LevelConfig cfg);
    void removeLevelConfig(std::string const& key);
    geode::Result<> save() const;

private:
    void loadFromDisk();

    std::map<std::string, LevelConfig> m_levels;
    bool m_loaded = false;
};

} // namespace autodeafen
