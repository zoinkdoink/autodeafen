#include "config_store.hpp"

#include <Geode/Geode.hpp>

#include <charconv>
#include <filesystem>

using namespace geode::prelude;

namespace autodeafen {

std::string levelKeyFor(GJGameLevel* level) {
    if (!level) return "";
    int id = level->m_levelID.value();
    if (id > 0) return "o:" + std::to_string(id);
    return "l:" + std::string(level->m_levelName);
}

static std::filesystem::path configPath() {
    return Mod::get()->getSaveDir() / "levels.json";
}

ConfigStore& ConfigStore::get() {
    static ConfigStore instance;
    return instance;
}

void ConfigStore::refresh() {
    loadFromDisk();
    m_loaded = true;
}

std::optional<LevelConfig> ConfigStore::levelConfig(std::string const& key) {
    if (!m_loaded) refresh();
    auto it = m_levels.find(key);
    if (it == m_levels.end()) return std::nullopt;
    return it->second;
}

void ConfigStore::setLevelConfig(std::string const& key, LevelConfig cfg) {
    if (!m_loaded) refresh();
    m_levels[key] = std::move(cfg);
}

void ConfigStore::removeLevelConfig(std::string const& key) {
    if (!m_loaded) refresh();
    m_levels.erase(key);
}

void ConfigStore::loadFromDisk() {
    m_levels.clear();
    auto path = configPath();
    if (!std::filesystem::exists(path)) return;

    auto read = utils::file::readJson(path);
    if (!read.isOk()) {
        log::warn("AutoDeafen: could not read {}: {}", path.string(), read.unwrapErr());
        return;
    }
    auto root = read.unwrap();
    auto levels = root.get("levels");
    if (!levels.isOk() || !levels.unwrap().isObject()) return;

    for (auto& entry : levels.unwrap()) {
        auto key = entry.getKey();
        if (!key || key->empty()) continue;

        LevelConfig cfg;
        if (auto enabled = entry.get("enabled"); enabled.isOk()) {
            cfg.enabled = enabled.unwrap().asBool().unwrapOr(false);
        }
        if (auto sp = entry.get("sp"); sp.isOk() && sp.unwrap().isObject()) {
            for (auto& item : sp.unwrap()) {
                auto spKey = item.getKey();
                if (!spKey) continue;
                int index = 0;
                auto [ptr, ec] = std::from_chars(
                    spKey->data(), spKey->data() + spKey->size(), index);
                if (ec != std::errc{} || ptr != spKey->data() + spKey->size() || index < 0) {
                    log::warn("AutoDeafen: ignoring bad startpos index '{}' in {}", *spKey, *key);
                    continue;
                }
                auto percent = item.asInt();
                if (!percent.isOk()) {
                    log::warn("AutoDeafen: ignoring non-integer percent for sp {} in {}", index, *key);
                    continue;
                }
                cfg.sp[index] = static_cast<int>(percent.unwrap());
            }
        }
        m_levels[*key] = std::move(cfg);
    }
    log::debug("AutoDeafen: loaded {} configured level(s)", m_levels.size());
}

geode::Result<> ConfigStore::save() const {
    auto levels = matjson::Value::object();
    for (auto const& [key, cfg] : m_levels) {
        auto sp = matjson::Value::object();
        for (auto const& [index, percent] : cfg.sp) {
            sp.set(std::to_string(index), matjson::Value(percent));
        }
        auto entry = matjson::Value::object();
        entry.set("enabled", matjson::Value(cfg.enabled));
        entry.set("sp", sp);
        levels.set(key, entry);
    }
    auto root = matjson::Value::object();
    root.set("schema", matjson::Value(1));
    root.set("levels", levels);
    return utils::file::writeStringSafe(configPath(), root.dump());
}

} // namespace autodeafen
