#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "config_store.hpp"
#include "core/resolve.hpp"
#include "deafen.hpp"
#include "level_config_popup.hpp"
#include "startpos.hpp"

#include <algorithm>

using namespace geode::prelude;

namespace {

// Whether a deafen has been requested this level session. Gates the un-deafen
// requests so untouched levels never wake the delivery backend.
bool s_sessionUsed = false;

} // namespace

class $modify(AutoDeafenPlayLayer, PlayLayer) {
    struct Fields {
        std::vector<StartPosObject*> m_startPositions;  // sorted by x
        std::optional<int> m_threshold;
        bool m_pendingResolve = false;
        bool m_deafenedThisAttempt = false;
        bool m_use21 = false;  // this level compares 2.1-style percents
    };

    // Current absolute percent in the basis the level's config uses.
    // getCurrentPercentInt is already absolute (it includes the startpos
    // offset); the 2.1 fallback formula is x-based and likewise absolute.
    int currentPercent() {
        if (m_fields.self()->m_use21) {
            if (!m_player1) return 0;
            return autodeafen::percentForPos(this, m_player1->getPosition(), true);
        }
        return this->getCurrentPercentInt();
    }

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        auto f = m_fields.self();
        f->m_startPositions.clear();
        if (!m_level || m_level->isPlatformer()) return;  // v1: classic levels only

        // Pick up hand edits to levels.json on every level enter.
        autodeafen::ConfigStore::get().refresh();

        f->m_startPositions = autodeafen::collectStartPositions(this);
        f->m_pendingResolve = true;
    }

    // 0 = level start, 1..N = StartPos objects by x order, matching the in-game
    // "StartPos n/N" switcher. -1 if the active startpos isn't in our list
    // (shouldn't happen; resolves to "no deafen").
    int activeStartPosIndex() {
        if (!m_startPosObject) return 0;
        auto f = m_fields.self();
        auto& v = f->m_startPositions;
        auto it = std::find(v.begin(), v.end(), m_startPosObject);
        if (it == v.end()) return -1;
        return static_cast<int>(it - v.begin()) + 1;
    }

    void resetLevel() {
        // Quick-restart and pause-menu restart skip the death/complete
        // hooks; clear our deafen here.
        if (m_fields.self()->m_deafenedThisAttempt) {
            this->undeafenIfNeeded("restart");
        }
        PlayLayer::resetLevel();
        // Defer resolution to the next postUpdate: by then the respawn has
        // fully applied (active startpos + spawn position are current even if
        // the startpos switcher updates m_startPosObject after resetLevel).
        auto f = m_fields.self();
        f->m_pendingResolve = true;
        f->m_deafenedThisAttempt = false;
    }

    void resolveNow() {
        auto f = m_fields.self();
        f->m_threshold = std::nullopt;
        if (!Mod::get()->getSettingValue<bool>("enabled")) return;
        if (!m_level || m_level->isPlatformer()) return;
        // m_isTestMode is set for any attempt that starts from a startpos,
        // so only practice mode is gated (opt-in via a global setting).
        if (m_isPracticeMode
            && !Mod::get()->getSettingValue<bool>("trigger-in-practice")) return;

        auto cfg = autodeafen::ConfigStore::get().levelConfig(
            autodeafen::levelKeyFor(m_level));
        f->m_use21 = cfg && cfg->use21;
        // Arming guard uses the startpos's own absolute percent: a threshold
        // at or below where you spawn can never be reached from there.
        int spawn = m_startPosObject
            ? autodeafen::percentForPos(this, m_startPosObject->getPosition(), f->m_use21)
            : 0;
        int spIndex = this->activeStartPosIndex();
        f->m_threshold = autodeafen::resolveThreshold(
            cfg ? &*cfg : nullptr, spIndex, spawn);
        std::string cfgThresh = "no entry";
        if (cfg) {
            if (auto it = cfg->sp.find(spIndex); it != cfg->sp.end()) {
                cfgThresh = fmt::format("{}%", it->second);
            }
        }
        log::debug("AutoDeafen resolve: sp={} spawn={}% cfg={} basis21={} -> {}",
            spIndex, spawn, cfgThresh, f->m_use21,
            f->m_threshold ? fmt::format("armed at {}%", *f->m_threshold)
                           : "not armed");
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        auto f = m_fields.self();
        if (f->m_pendingResolve) {
            f->m_pendingResolve = false;
            this->resolveNow();
        }
        if (!f->m_threshold) return;
        if (this->currentPercent() >= *f->m_threshold) {
            int fired = *f->m_threshold;
            f->m_threshold = std::nullopt;  // at most once per attempt
            f->m_deafenedThisAttempt = true;
            s_sessionUsed = true;
            log::info("AutoDeafen: deafening at {}%", fired);
            autodeafen::deafen::request(true);
        }
    }

    void pauseGame(bool unfocused) {
        PlayLayer::pauseGame(unfocused);
        if (m_fields.self()->m_deafenedThisAttempt
            && Mod::get()->getSettingValue<bool>("undeafen-on-pause")) {
            autodeafen::deafen::request(false);
        }
    }

    void resume() {
        PlayLayer::resume();
        if (m_fields.self()->m_deafenedThisAttempt
            && Mod::get()->getSettingValue<bool>("undeafen-on-pause")) {
            autodeafen::deafen::request(true);
        }
    }

    void undeafenIfNeeded(char const* why) {
        if (!s_sessionUsed) return;
        m_fields.self()->m_deafenedThisAttempt = false;
        log::info("AutoDeafen: un-deafen requested ({})", why);
        autodeafen::deafen::request(false);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        PlayLayer::destroyPlayer(player, object);
        // destroyPlayer also fires for non-deaths: the anticheat integrity
        // check, and discarding the second player at dual-portal exits.
        // m_isDead confirms the attempt actually ended.
        if (object != m_anticheatSpike && m_player1 && m_player1->m_isDead) {
            this->undeafenIfNeeded("death");
        }
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        this->undeafenIfNeeded("level complete");
    }

    void onQuit() {
        this->undeafenIfNeeded("quit");
        s_sessionUsed = false;
        PlayLayer::onQuit();
    }
};

class $modify(AutoDeafenPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto playLayer = PlayLayer::get();
        if (!playLayer || !playLayer->m_level || playLayer->m_level->isPlatformer()) {
            return;
        }
        auto menu = this->getChildByID("left-button-menu");
        if (!menu) return;

        CCNode* sprite = CCSprite::createWithSpriteFrameName("gj_discordIcon_001.png");
        if (!sprite) {
            sprite = ButtonSprite::create("AD", "goldFont.fnt", "GJ_button_01.png", .8f);
            sprite->setScale(.6f);
        }
        auto button = CCMenuItemSpriteExtra::create(
            sprite, this, menu_selector(AutoDeafenPauseLayer::onAutoDeafen));
        button->setID("config-button"_spr);
        menu->addChild(button);
        menu->updateLayout();
    }

    void onAutoDeafen(CCObject*) {
        if (auto popup = autodeafen::LevelConfigPopup::create(PlayLayer::get())) {
            popup->show();
        }
    }
};
