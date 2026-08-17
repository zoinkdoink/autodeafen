#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

#include <string>
#include <vector>

namespace autodeafen {

// Pause-menu popup for the per-level config: a master enable toggle plus one
// row per spawn point (level start + each startpos) with a percent input.
// Empty input = no deafen from that spawn point. Saves on close.
class LevelConfigPopup : public geode::Popup {
public:
    static LevelConfigPopup* create(PlayLayer* playLayer);

protected:
    bool initWith(PlayLayer* playLayer);
    void onClose(cocos2d::CCObject* sender) override;
    void onToggleEnabled(cocos2d::CCObject* sender);
    void onToggle21(cocos2d::CCObject* sender);
    void onSettings(cocos2d::CCObject* sender);
    void refreshRowLabels();
    void saveConfig();

    PlayLayer* m_playLayer = nullptr;
    std::string m_levelKey;
    bool m_enabled = false;
    bool m_use21 = false;
    std::vector<geode::TextInput*> m_inputs;        // index 0 = level start
    std::vector<cocos2d::CCLabelBMFont*> m_rowLabels;
    std::vector<cocos2d::CCPoint> m_rowPositions;   // parallel to labels
};

} // namespace autodeafen
