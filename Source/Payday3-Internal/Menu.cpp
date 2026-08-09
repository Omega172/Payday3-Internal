#include <map>
#include <span>
#include <array>
#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <cstdlib>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#undef min
#undef max

#include <imgui.h>
#include <imgui_internal.h>
#include "Dumper-7/SDK.hpp"
#include "Config.hpp"
#include "Utils/Logging.hpp"
#include "Features/ESP/ESP.hpp"
#include "Features/Features.hpp"
#include "Menu.hpp"

namespace
{
    constexpr int g_iConfigVersion = 1;

    std::string Trim(std::string_view svValue)
    {
        size_t iBegin = 0;
        size_t iEnd = svValue.size();

        while (iBegin < iEnd && std::isspace(static_cast<unsigned char>(svValue[iBegin])))
            ++iBegin;

        while (iEnd > iBegin && std::isspace(static_cast<unsigned char>(svValue[iEnd - 1])))
            --iEnd;

        return std::string(svValue.substr(iBegin, iEnd - iBegin));
    }

    bool TryParseBool(const std::string& sValue, bool& bOut)
    {
        std::string sNormalized = sValue;
        std::transform(sNormalized.begin(), sNormalized.end(), sNormalized.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        if (sNormalized == "1" || sNormalized == "true")
        {
            bOut = true;
            return true;
        }

        if (sNormalized == "0" || sNormalized == "false")
        {
            bOut = false;
            return true;
        }

        return false;
    }

    bool TryParseInt(const std::string& sValue, int& iOut)
    {
        try
        {
            size_t iPos = 0;
            const int iParsed = std::stoi(sValue, &iPos);
            if (iPos != sValue.size())
                return false;

            iOut = iParsed;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool TryParseFloat(const std::string& sValue, float& flOut)
    {
        try
        {
            size_t iPos = 0;
            const float flParsed = std::stof(sValue, &iPos);
            if (iPos != sValue.size())
                return false;

            flOut = flParsed;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    std::filesystem::path GetConfigPath()
    {
        char szModulePath[MAX_PATH]{};
        if (GetModuleFileNameA(nullptr, szModulePath, MAX_PATH) == 0)
            return "payday3_internal.cfg";

        std::filesystem::path pathConfigDir = std::filesystem::path(szModulePath).parent_path() / "Configs";
        std::error_code ec{};
        std::filesystem::create_directories(pathConfigDir, ec);

        return pathConfigDir / "payday3_internal.cfg";
    }
}

void MultiSelectInternal(const char* szLabel, std::string& sPreviewText, std::span<std::tuple<const char*, const char*, bool&>> aEntries){
    if(!sPreviewText.size()){
        sPreviewText = "";

        for(const auto& tupleEntry : aEntries){
            if(!std::get<2>(tupleEntry))
                continue;

            if(sPreviewText.size())
                sPreviewText += ", ";
            
            sPreviewText += std::get<1>(tupleEntry);
        }
        
        sPreviewText = std::format("{}###{}", (!sPreviewText.size()) ? "None" : sPreviewText, szLabel);
    }

    if(!ImGui::BeginCombo(szLabel, sPreviewText.c_str()))
        return;

    ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
    
    bool bChanged = false;
    for(const auto& tupleEntry : aEntries)
        bChanged |= ImGui::Selectable(std::get<0>(tupleEntry), &std::get<2>(tupleEntry));

    ImGui::PopItemFlag();
    ImGui::EndCombo();

    if(!bChanged)
        return;

    sPreviewText = "";

    for(const auto& tupleEntry : aEntries){
        if(!std::get<2>(tupleEntry))
            continue;

        if(sPreviewText.size())
            sPreviewText += ", ";
        
        sPreviewText += std::get<1>(tupleEntry);
    }
    
    sPreviewText = std::format("{}###{}", (!sPreviewText.size()) ? "None" : sPreviewText, szLabel);
};

#define MultiSelect(szLabel, aData) { \
    static std::string sMultiSelectPreview{};\
    static auto aMultiSelectData = std::to_array<std::tuple<const char*, const char*, bool&>>aData;\
    MultiSelectInternal(szLabel, sMultiSelectPreview, aMultiSelectData);\
}

void Hotkey(const char* szLabel, Menu::Hotkey_t& bind){
    const auto id = ImGui::GetID(szLabel);
    ImGui::PushID(id);

    ImGui::TextUnformatted(szLabel);

    if(!bind.m_bFixedType){
        ImGui::SameLine();
        static const char* aTypes[] = { "Off", "On", "Hold", "Hold Off", "Toggle" };
        int iItem = static_cast<int>(bind.m_eType);
        ImGui::Combo("", &iItem, aTypes, IM_ARRAYSIZE(aTypes));
        bind.m_eType = static_cast<Menu::Hotkey_t::EType>(iItem);
    }
    
    if(bind.m_eType != Menu::Hotkey_t::EType::AlwaysOff && bind.m_eType != Menu::Hotkey_t::EType::AlwaysOn){
        ImGui::SameLine();
        if(ImGui::GetActiveID() == id){
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_ButtonActive));
            ImGui::Button("...");
            ImGui::PopStyleColor();
            ImGui::SetKeyOwner(ImGuiKey_Escape, id);

            ImGui::GetCurrentContext()->ActiveIdAllowOverlap = true;
            ImGui::SetActiveID(id, ImGui::GetCurrentWindow());
            if((!ImGui::IsItemHovered() && ImGui::GetIO().MouseClicked[0]) || bind.SetToPressedKey())
                ImGui::ClearActiveID();
        } else if(ImGui::Button(bind.ToString()))
            ImGui::SetActiveID(id, ImGui::GetCurrentWindow());
    }

    ImGui::PopID();
}


bool CheatConfig::Save() const{

    const auto pathConfig = GetConfigPath();
    std::ofstream fileConfig(pathConfig, std::ios::trunc);
    if (!fileConfig.is_open() || fileConfig.fail())
    {
        Utils::LogError(std::format("Failed to open config for writing: {}", pathConfig.string()));
        return false;
    }

    auto Write = [&](const char* szKey, const auto& value){
        using T = std::decay_t<decltype(value)>;

        if constexpr(std::is_enum_v<T>){
            fileConfig << szKey << '=' << static_cast<int>(value) << '\n';
        }
        else if constexpr(std::is_integral_v<T> || std::is_floating_point_v<T>){
            fileConfig << szKey << '=' << value << '\n';
        }
        else if constexpr(std::is_same_v<T, bool>){
            fileConfig << szKey << '=' << (value ? 1 : 0) << '\n';
        }
        else if constexpr(std::is_same_v<T, Menu::Hotkey_t>){
            fileConfig << szKey << ".key=" << static_cast<int>(value.m_eKeyCode) << '\n';
            if(value.m_bFixedType)
                fileConfig << szKey << ".type=" << static_cast<int>(value.m_eType) << '\n';
        }
    };

    fileConfig << "# Payday3-Internal config\n";
    Write("config.version", g_iConfigVersion);

    const auto& espConfig = ESP::GetConfig();
    Write("aimbot.enabled", m_aimbot.m_bEnabled);
    Write("aimbot.fov", m_aimbot.m_flAimFOV);
    Write("aimbot.fovCircle", espConfig.bDrawFovCircle);
    Write("aimbot.sorting", static_cast<int>(m_aimbot.m_eSorting));
    Write("aimbot.targets.guards", m_aimbot.m_bGuards);
    Write("aimbot.targets.specials", m_aimbot.m_bSpecials);
    Write("aimbot.targets.fbiVan", m_aimbot.m_bFBIVan);
    Write("aimbot.targets.civilians", m_aimbot.m_bCivilians);
    Write("aimbot.throughWalls", m_aimbot.m_bThroughWalls);
    Write("aimbot.disableInStealth", m_aimbot.m_bDisableInStealth);

    Write("misc.keyClientMove", m_misc.m_keyClientMove);
    Write("misc.keyClientMoveTeleport", m_misc.m_keyClientMoveTeleport);
    Write("misc.keyClientMoveFaster", m_misc.m_keyClientMoveFaster);

    Write("misc.clientMove.autoTeleport", m_misc.m_bClientMoveAutoTeleport);
    Write("misc.clientMove.baseSpeed", m_misc.m_flClientMoveBaseSpeed);

    Write("misc.noSpread", m_misc.m_bNoSpread);
    Write("misc.noRecoil", m_misc.m_bNoRecoil);
    Write("misc.noFallDamage", m_misc.m_bNoFallDamage);
    Write("misc.instantInteraction", m_misc.m_bInstantInteraction);
    Write("misc.instantMinigame", m_misc.m_bInstantMinigame);
    Write("misc.instantReload", m_misc.m_bInstantReload);
    Write("misc.instantMelee", m_misc.m_bInstantMelee);
    Write("misc.autoPistol", m_misc.m_bAutoPistol);

    Write("misc.speedBuff", m_misc.m_bSpeedBuff);
    Write("misc.damageBuff", m_misc.m_bDamageBuff);
    Write("misc.armorBuff", m_misc.m_bArmorBuff);

    Write("misc.noCameraShake", m_misc.m_bNoCameraShake);
    Write("misc.noCameraTilt", m_misc.m_bNoCameraTilt);
    Write("misc.cameraFov", m_misc.m_flCameraFOV);

    Write("misc.rapidFire", m_misc.m_iRapidFire);

    Write("misc.moreBullets.enabled", m_misc.m_bMoreBullets);
    Write("misc.moreBullets.count", m_misc.m_iMoreBullets);

    Write("misc.superToss.enabled", m_misc.m_bSuperToss);
    Write("misc.superToss.velocity", m_misc.m_flSuperToss);

    Write("esp.enabled", espConfig.bESP);
    Write("esp.normal.box", espConfig.m_stNormalEnemies.m_bBox);
    Write("esp.normal.health", espConfig.m_stNormalEnemies.m_bHealth);
    Write("esp.normal.armor", espConfig.m_stNormalEnemies.m_bArmor);
    Write("esp.normal.name", espConfig.m_stNormalEnemies.m_bName);
    Write("esp.normal.flags", espConfig.m_stNormalEnemies.m_bFlags);
    Write("esp.normal.skeleton", espConfig.m_stNormalEnemies.m_bSkeleton);
    Write("esp.normal.outline", espConfig.m_stNormalEnemies.m_bOutline);

    Write("esp.special.box", espConfig.m_stSpecialEnemies.m_bBox);
    Write("esp.special.health", espConfig.m_stSpecialEnemies.m_bHealth);
    Write("esp.special.armor", espConfig.m_stSpecialEnemies.m_bArmor);
    Write("esp.special.name", espConfig.m_stSpecialEnemies.m_bName);
    Write("esp.special.flags", espConfig.m_stSpecialEnemies.m_bFlags);
    Write("esp.special.skeleton", espConfig.m_stSpecialEnemies.m_bSkeleton);
    Write("esp.special.outline", espConfig.m_stSpecialEnemies.m_bOutline);

    Write("esp.civilians.box", espConfig.m_stCivilians.m_bBox);
    Write("esp.civilians.flags", espConfig.m_stCivilians.m_bFlags);
    Write("esp.civilians.skeleton", espConfig.m_stCivilians.m_bSkeleton);
    Write("esp.civilians.outline", espConfig.m_stCivilians.m_bOutline);
    Write("esp.civilians.onlyWhenSpecial", espConfig.m_stCivilians.m_bOnlyWhenSpecial);

    Write("esp.debug.skeleton", espConfig.bDebugSkeleton);
    Write("esp.debug.drawBoneIndices", espConfig.bDebugDrawBoneIndices);
    Write("esp.debug.drawBoneNames", espConfig.bDebugDrawBoneNames);
    Write("esp.debug.skeletonDrawBoneIndices", espConfig.bDebugSkeletonDrawBoneIndices);
    Write("esp.debug.skeletonDrawBoneNames", espConfig.bDebugSkeletonDrawBoneNames);
    Write("esp.debug.esp", espConfig.bDebugESP);

    LootESP::Config& lootespConfig = LootESP::GetConfig();
    Write("lootesp.enabled", lootespConfig.bLootESP);

    if (fileConfig.fail())
    {
        Utils::LogError(std::format("Failed to write config: {}", pathConfig.string()));
        return false;
    }

    Utils::LogDebug(std::format("Config saved: {}", pathConfig.string()));
    return true;
}

bool CheatConfig::Load()
{
    const auto pathConfig = GetConfigPath();
    std::ifstream fileConfig(pathConfig);
    if (!fileConfig.is_open() || fileConfig.fail())
    {
        Utils::LogDebug(std::format("Config file not found, using defaults: {}", pathConfig.string()));
        return false;
    }

    std::unordered_map<std::string, std::string> mapConfigValues{};
    std::string sLine{};

    while (std::getline(fileConfig, sLine))
    {
        const std::string sTrimmed = Trim(sLine);
        if (sTrimmed.empty() || sTrimmed[0] == '#' || sTrimmed[0] == ';')
            continue;

        const size_t iEquals = sTrimmed.find('=');
        if (iEquals == std::string::npos)
            continue;

        const std::string sKey = Trim(std::string_view(sTrimmed).substr(0, iEquals));
        const std::string sValue = Trim(std::string_view(sTrimmed).substr(iEquals + 1));
        if (!sKey.empty())
            mapConfigValues[sKey] = sValue;
    }

    auto Read = [&](const char* szKey, auto& value) -> bool {
        using T = std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T, Menu::Hotkey_t>){
            const auto itr1 = mapConfigValues.find((std::string{szKey} + ".key").c_str());
            const auto itr2 = mapConfigValues.find((std::string{szKey} + ".type").c_str());

            bool bFailed = true;
            if(itr1 != mapConfigValues.end()){
                int iParsed{};
                if(TryParseInt(itr1->second, iParsed)){
                    bFailed = false;
                    value.m_eKeyCode = static_cast<ImGuiKey>(iParsed);
                    if(iParsed < static_cast<int>(ImGuiKey_NamedKey_BEGIN) || iParsed > static_cast<int>(ImGuiKey_NamedKey_END))
                        value.m_eKeyCode = ImGuiKey_None;

                }
            }

            if(value.m_bFixedType)
                return bFailed;

            if(itr2 == mapConfigValues.end())
                return false;

            int iParsed{};
            if(!TryParseInt(itr2->second, iParsed))
                return false;

            value.m_eType = static_cast<Menu::Hotkey_t::EType>(iParsed);
            if(iParsed < static_cast<int>(Menu::Hotkey_t::EType::AlwaysOff) || iParsed > static_cast<int>(Menu::Hotkey_t::EType::Toggle))
                value.m_eType = Menu::Hotkey_t::EType::Hold;
            
            return bFailed;
        }

        const auto itr = mapConfigValues.find(szKey);
        if(itr == mapConfigValues.end())
            return false;
        
        if constexpr(std::is_same_v<T, bool>){
            bool bParsed{};
            if(!TryParseBool(itr->second, bParsed))
                return false;

            value = bParsed;
            return true;
        }
        else if constexpr(std::is_integral_v<T>){
            int iParsed{};
            if(!TryParseInt(itr->second, iParsed))
                return false;

            value = iParsed;
            return true;
        }
        else if constexpr(std::is_floating_point_v<T>){
            float flParsed{};
            if(!TryParseFloat(itr->second, flParsed))
                return false;

            value = flParsed;
            return true;
        }
    };

    int iVersion{};
    auto& espConfig = ESP::GetConfig();

    if (Read("config.version", iVersion) && iVersion != g_iConfigVersion)
        Utils::LogDebug(std::format("Loading config version {} with parser version {}", iVersion, g_iConfigVersion));

    Read("aimbot.enabled", m_aimbot.m_bEnabled);
    if (Read("aimbot.fov", m_aimbot.m_flAimFOV))
        m_aimbot.m_flAimFOV = std::clamp(m_aimbot.m_flAimFOV, 0.0f, 180.0f);

    int iSorting{};

    Read("aimbot.fovCircle", espConfig.bDrawFovCircle);
    if (Read("aimbot.sorting", iSorting)
        && iSorting >= static_cast<int>(Aimbot_t::ESorting::Smart)
        && iSorting <= static_cast<int>(Aimbot_t::ESorting::Threat))
    {
        m_aimbot.m_eSorting = static_cast<Aimbot_t::ESorting>(iSorting);
    }

    Read("aimbot.targets.guards", m_aimbot.m_bGuards);
    Read("aimbot.targets.specials", m_aimbot.m_bSpecials);
    Read("aimbot.targets.fbiVan", m_aimbot.m_bFBIVan);
    Read("aimbot.targets.civilians", m_aimbot.m_bCivilians);
    Read("aimbot.throughWalls", m_aimbot.m_bThroughWalls);
    Read("aimbot.disableInStealth", m_aimbot.m_bDisableInStealth);

    Read("misc.keyClientMove", m_misc.m_keyClientMove);
    Read("misc.keyClientMoveTeleport", m_misc.m_keyClientMoveTeleport);
    Read("misc.keyClientMoveFaster", m_misc.m_keyClientMoveFaster);

    Read("misc.clientMove.autoTeleport", m_misc.m_bClientMoveAutoTeleport);
    if (Read("misc.clientMove.baseSpeed", m_misc.m_flClientMoveBaseSpeed))
        m_misc.m_flClientMoveBaseSpeed = std::max(0.0f, m_misc.m_flClientMoveBaseSpeed);

    Read("misc.noSpread", m_misc.m_bNoSpread);
    Read("misc.noRecoil", m_misc.m_bNoRecoil);
    Read("misc.noFallDamage", m_misc.m_bNoFallDamage);
    Read("misc.instantInteraction", m_misc.m_bInstantInteraction);
    Read("misc.instantMinigame", m_misc.m_bInstantMinigame);
    Read("misc.instantReload", m_misc.m_bInstantReload);
    Read("misc.instantMelee", m_misc.m_bInstantMelee);
    Read("misc.autoPistol", m_misc.m_bAutoPistol);

    Read("misc.speedBuff", m_misc.m_bSpeedBuff);
    Read("misc.damageBuff", m_misc.m_bDamageBuff);
    Read("misc.armorBuff", m_misc.m_bArmorBuff);

    Read("misc.noCameraShake", m_misc.m_bNoCameraShake);
    Read("misc.noCameraTilt", m_misc.m_bNoCameraTilt);
    Read("misc.cameraFov", m_misc.m_flCameraFOV);

    int iRapidFire{};
    if (Read("misc.rapidFire", iRapidFire))
        m_misc.m_iRapidFire = std::clamp(iRapidFire, 0, 2);

    Read("misc.moreBullets.enabled", m_misc.m_bMoreBullets);
    int iMoreBullets{};
    if (Read("misc.moreBullets.count", iMoreBullets))
        m_misc.m_iMoreBullets = std::max(1, iMoreBullets);

    Read("misc.superToss.enabled", m_misc.m_bSuperToss);
    if (Read("misc.superToss.velocity", m_misc.m_flSuperToss))
        m_misc.m_flSuperToss = std::max(0.0f, m_misc.m_flSuperToss);

    Read("esp.enabled", espConfig.bESP);

    Read("esp.normal.box", espConfig.m_stNormalEnemies.m_bBox);
    Read("esp.normal.health", espConfig.m_stNormalEnemies.m_bHealth);
    Read("esp.normal.armor", espConfig.m_stNormalEnemies.m_bArmor);
    Read("esp.normal.name", espConfig.m_stNormalEnemies.m_bName);
    Read("esp.normal.flags", espConfig.m_stNormalEnemies.m_bFlags);
    Read("esp.normal.skeleton", espConfig.m_stNormalEnemies.m_bSkeleton);
    Read("esp.normal.outline", espConfig.m_stNormalEnemies.m_bOutline);

    Read("esp.special.box", espConfig.m_stSpecialEnemies.m_bBox);
    Read("esp.special.health", espConfig.m_stSpecialEnemies.m_bHealth);
    Read("esp.special.armor", espConfig.m_stSpecialEnemies.m_bArmor);
    Read("esp.special.name", espConfig.m_stSpecialEnemies.m_bName);
    Read("esp.special.flags", espConfig.m_stSpecialEnemies.m_bFlags);
    Read("esp.special.skeleton", espConfig.m_stSpecialEnemies.m_bSkeleton);
    Read("esp.special.outline", espConfig.m_stSpecialEnemies.m_bOutline);

    Read("esp.civilians.box", espConfig.m_stCivilians.m_bBox);
    Read("esp.civilians.flags", espConfig.m_stCivilians.m_bFlags);
    Read("esp.civilians.skeleton", espConfig.m_stCivilians.m_bSkeleton);
    Read("esp.civilians.outline", espConfig.m_stCivilians.m_bOutline);
    Read("esp.civilians.onlyWhenSpecial", espConfig.m_stCivilians.m_bOnlyWhenSpecial);

    Read("esp.debug.skeleton", espConfig.bDebugSkeleton);
    Read("esp.debug.drawBoneIndices", espConfig.bDebugDrawBoneIndices);
    Read("esp.debug.drawBoneNames", espConfig.bDebugDrawBoneNames);
    Read("esp.debug.skeletonDrawBoneIndices", espConfig.bDebugSkeletonDrawBoneIndices);
    Read("esp.debug.skeletonDrawBoneNames", espConfig.bDebugSkeletonDrawBoneNames);
    Read("esp.debug.esp", espConfig.bDebugESP);

    m_misc.m_keyClientMove.m_bActive = false;
    m_misc.m_keyClientMove.m_bPressedThisFrame = false;
    m_misc.m_keyClientMoveTeleport.m_bActive = false;
    m_misc.m_keyClientMoveTeleport.m_bPressedThisFrame = false;
    m_misc.m_keyClientMoveFaster.m_bActive = false;
    m_misc.m_keyClientMoveFaster.m_bPressedThisFrame = false;

    auto& lootespConfig = LootESP::GetConfig();
    Read("lootesp.enabled", lootespConfig.bLootESP);

    Utils::LogDebug(std::format("Config loaded: {}", pathConfig.string()));
    return true;
}


void CheatConfig::Aimbot_t::Draw(){
    ImGui::Checkbox("Enabled", &m_bEnabled);
    if(!m_bEnabled)
        return;

    ImGui::SliderFloat("Aim FOV", &m_flAimFOV, 0.f, 180.f, "%0.0f");

    ImGui::Checkbox("Draw FOV Circle", &ESP::GetConfig().bDrawFovCircle);

    static const char* aSortingItems[]{ "Smart", "FOV", "Threat" };
    ImGui::Combo("Sorting Method", reinterpret_cast<int*>(&m_eSorting), aSortingItems, IM_ARRAYSIZE(aSortingItems));

    MultiSelect("Targets", ({
        {"Guards", "Guards", m_bGuards},
        {"Specials", "Specials", m_bSpecials},
        {"FBI Van", "Van", m_bFBIVan},
        {"Civilians", "Civs", m_bCivilians}
    }));

    ImGui::Checkbox("Aim Through Walls", &m_bThroughWalls);
    ImGui::Checkbox("Loud Only", &m_bDisableInStealth);
}

void CheatConfig::Visuals_t::Draw(){
    auto& espConfig = ESP::GetConfig();
    ImGui::Checkbox("Enable ESP", &espConfig.bESP);
    if (espConfig.bESP) {
        ImGui::Indent();

        MultiSelect("Normal Enemies", ({
            {"Box", "B", espConfig.m_stNormalEnemies.m_bBox},
            {"Health", "H", espConfig.m_stNormalEnemies.m_bHealth},
            {"Armor", "A", espConfig.m_stNormalEnemies.m_bArmor},
            {"Name", "N", espConfig.m_stNormalEnemies.m_bName},
            {"Flags", "F", espConfig.m_stNormalEnemies.m_bFlags},
            {"Skeleton", "S", espConfig.m_stNormalEnemies.m_bSkeleton},
            {"Outline", "O", espConfig.m_stNormalEnemies.m_bOutline}
        }));

        MultiSelect("Special Enemies", ({
            {"Box", "B", espConfig.m_stSpecialEnemies.m_bBox},
            {"Health", "H", espConfig.m_stSpecialEnemies.m_bHealth},
            {"Armor", "A", espConfig.m_stSpecialEnemies.m_bArmor},
            {"Name", "N", espConfig.m_stSpecialEnemies.m_bName},
            {"Flags", "F", espConfig.m_stSpecialEnemies.m_bFlags},
            {"Skeleton", "S", espConfig.m_stSpecialEnemies.m_bSkeleton},
            {"Outline", "O", espConfig.m_stSpecialEnemies.m_bOutline}
        }));

        MultiSelect("Civilians", ({
            {"Box", "B", espConfig.m_stCivilians.m_bBox},
            {"Flags", "F", espConfig.m_stCivilians.m_bFlags},
            {"Skeleton", "S", espConfig.m_stCivilians.m_bSkeleton},
            {"Outline", "O", espConfig.m_stCivilians.m_bOutline},
            {"Only When Special Enemy is Visible", "OnlyWhenSpecial", espConfig.m_stCivilians.m_bOnlyWhenSpecial}
        }));

#ifdef _DEBUG
        ImGui::Checkbox("Debug Draw Bone Indices", &espConfig.bDebugDrawBoneIndices);
        ImGui::Checkbox("Debug Draw Bone Names Instead of Indices", &espConfig.bDebugDrawBoneNames);
#endif
        
#ifdef _DEBUG
        ImGui::Checkbox("Debug Skeleton", &espConfig.bDebugSkeleton);
        if (espConfig.bDebugSkeleton) {
            ImGui::Indent();
            ImGui::Checkbox("Debug Skeleton Draw Bone Indices", &espConfig.bDebugSkeletonDrawBoneIndices);
            ImGui::Checkbox("Debug Skeleton Draw Bone Names Instead of Indices", &espConfig.bDebugSkeletonDrawBoneNames);
            ImGui::Unindent();
        }
#endif
        ImGui::Unindent();
    }

    auto& lootespConfig = LootESP::GetConfig();
    ImGui::Checkbox("Enable Loot ESP", &lootespConfig.bLootESP);
    if (lootespConfig.bLootESP) {
        // Loot ESP options
    }

#ifdef _DEBUG
    ImGui::Checkbox("Debug ESP (Show Class Names)", &espConfig.bDebugESP);
#endif
}




void CheatConfig::Misc_t::Draw(){
    Hotkey("Client Move", m_keyClientMove);
    if(m_keyClientMove.m_eType != Menu::Hotkey_t::EType::AlwaysOff){
        ImGui::Indent();
        Hotkey("Teleport", m_keyClientMoveTeleport);
        Hotkey("Move Faster", m_keyClientMoveFaster);
        ImGui::SliderFloat("Speed###ClientMoveSpeed", &m_flClientMoveBaseSpeed, 500.f, 5000.f);
        ImGui::Checkbox("Auto Teleport", &m_bClientMoveAutoTeleport);
        ImGui::Unindent();
    }
    

    MultiSelect("Removals", ({
        {"No Spread", "Spread", m_bNoSpread},
        {"No Recoil", "Recoil", m_bNoRecoil},
        {"No Fall Damage", "Fall", m_bNoFallDamage},
        {"Instant Interaction", "Interact", m_bInstantInteraction},
        {"Instant Minigame", "Minigame", m_bInstantMinigame},
        {"Instant Reload", "Reload", m_bInstantReload},
        {"Instant Melee", "Melee", m_bInstantMelee},
        {"Auto Pistol", "Auto", m_bAutoPistol}
    }));

    MultiSelect("Camera Modifiers", ({
        {"Disable Shake", "Shake", m_bNoCameraShake},
        {"Disable Tilt", "Tilt", m_bNoCameraTilt}
    }));

    ImGui::SliderFloat("Camera FOV", &m_flCameraFOV, 0.f, 150.f, "%0.0f");

    MultiSelect("Buffs", ({
        {"Speed", "Speed", m_bSpeedBuff},
        {"Damage", "Damage", m_bDamageBuff},
        {"Armor", "Armor", m_bArmorBuff}
    }));

    static const char* aRapidFireOptions[]{ "Disabled", "Steady", "Rapid" };
    ImGui::Combo("Rapid Fire", &m_iRapidFire, aRapidFireOptions, IM_ARRAYSIZE(aRapidFireOptions));

    ImGui::Checkbox("More Bullets", &m_bMoreBullets);
    if(m_bMoreBullets){
        ImGui::SameLine();
        ImGui::SliderInt("###More Bullets Count", &m_iMoreBullets, 1, 100, "%d", ImGuiSliderFlags_AlwaysClamp);
    }
    
    ImGui::Checkbox("Super Toss", &m_bSuperToss);
    if(m_bSuperToss){
        ImGui::SameLine();
        ImGui::SliderFloat("###Super Toss Speed", &m_flSuperToss, 1000.f, 5000.f);
    }
}

namespace Menu
{
    void CallTraceEntry_t::Draw()
    {
        if(!g_sCallTraceFilter.empty() && !m_sClassName.contains(g_sCallTraceFilter)){
            if(!g_bCallTraceFilterSubclasses)
                return;
            
            if(std::find_if(m_vecSubClasses.begin(), m_vecSubClasses.end(), [&](const std::string& str) {
                return str.contains(g_sCallTraceFilter);
            }) == m_vecSubClasses.end())
                return;
        }

        if(ImGui::CollapsingHeader(m_sClassName.c_str()))
        {
            ImGui::PushID(m_sClassName.c_str());

            if(m_vecSubClasses.size()){
                if(ImGui::TreeNode("Sub Classes"))
                {
                    for(const auto& str : m_vecSubClasses)
                        ImGui::Text("%s", str.c_str());
                    ImGui::TreePop();
                }
            }
            
            if(ImGui::TreeNode("Called Functions"))
            {
                for(const auto& pairEntry : m_mapCalledFunctions)
                    ImGui::Text("%s", pairEntry.second.c_str());
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    void PreDraw()
    {
        CheatConfig::Get().m_misc.m_keyClientMove.UpdateState();
        CheatConfig::Get().m_misc.m_keyClientMoveTeleport.UpdateState();
        CheatConfig::Get().m_misc.m_keyClientMoveFaster.UpdateState();

        try {
            SDK::UWorld* pGWorld = SDK::UWorld::GetWorld();
            if (!pGWorld)
                return;

            SDK::UGameInstance* pGameInstance = pGWorld->OwningGameInstance;
            if (!pGameInstance)
                return;

            // Empty during menu/loading — TArray::operator[] throws and kills the process
            if (!pGameInstance->LocalPlayers.IsValidIndex(0))
                return;

            SDK::ULocalPlayer* pLocalPlayer = pGameInstance->LocalPlayers[0];
            if (!pLocalPlayer)
                return;

            SDK::APlayerController* pPlayerController = pLocalPlayer->PlayerController;
            if (!pPlayerController)
                return;

            SDK::ULevel* pPersistentLevel = pGWorld->PersistentLevel;
            if (!pPersistentLevel)
                return;

            ESP::Render(pGWorld, pPlayerController);
            ESP::RenderDebugESP(pPersistentLevel, pPlayerController);
        } catch (const std::exception&) {
            // Present-thread must not throw; skip this frame's world overlay
        } catch (...) {
        }
    }

    

	// Draw the main menu content
	void Draw(bool& bShowMenu)
	{
		if (!bShowMenu)
			return;

        std::string windowTitle = std::format("OmegaWare PD3 Internal - {}", CURRENT_VERSION);
        ImGui::Begin(windowTitle.c_str(), &bShowMenu, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

        if(ImGui::BeginTabBar("CheatTabs"))
        {
            if(ImGui::BeginTabItem("Aimbot")){
                CheatConfig::Get().m_aimbot.Draw();
                ImGui::EndTabItem();
            }

            if(ImGui::BeginTabItem("Visuals")){
                CheatConfig::Get().m_visuals.Draw();
                ImGui::EndTabItem();
            }

            if(ImGui::BeginTabItem("Misc")){
                CheatConfig::Get().m_misc.Draw();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Debug")){
                auto sHostStatus = [](){
                    SDK::UWorld* pGWorld = SDK::UWorld::GetWorld();
                    if (!pGWorld)
                        return "Unknown";

                    SDK::UGameInstance* pGameInstance = pGWorld->OwningGameInstance;
                    if (!pGameInstance || !pGameInstance->IsA(SDK::USBZGameInstance::StaticClass()))
                        return "Unknown";

                    SDK::USBZGameInstance* pSBZGameInstance = static_cast<SDK::USBZGameInstance*>(pGameInstance);
                    auto pAccelByteUser = pSBZGameInstance->AccelByteUser;
                    if (!pAccelByteUser)
                        return "Unknown";

                    auto pUserActivity = pAccelByteUser->UserActivity;
                    return pUserActivity.bIsHost ? "Host" : "Client";    
                };
                ImGui::Text(std::format("bIsHost: {}", sHostStatus()).c_str());
                ImGui::EndTabItem();
            }


            ImGui::EndTabBar();
        }

        ImGui::Separator();
        if (ImGui::Button("Save Config"))
            CheatConfig::Get().Save();

        ImGui::SameLine();
        if (ImGui::Button("Load Config"))
            CheatConfig::Get().Load();

        ImGui::SameLine();
		
		// Performance metrics
		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

        ImGui::End();

#ifdef _DEBUG
        ImGui::Begin("Call Traces",  &bShowMenu);
        static const char* aCallTraceItems[] = { "Inactive", "UObject", "PlayerController" };
        if(ImGui::Combo("Call Trace Area", reinterpret_cast<int*>(&g_eCallTraceArea), aCallTraceItems, IM_ARRAYSIZE(aCallTraceItems)))
            g_mapCallTraces.clear();
        
        ImGui::InputText("##Filter", g_szCallTraceFilter, sizeof(g_szCallTraceFilter));
        ImGui::SameLine();
        ImGui::Checkbox("##Filter Use Subclasses", &g_bCallTraceFilterSubclasses);

        g_sCallTraceFilter = g_szCallTraceFilter;

        ImGui::Separator();
        for (auto& pairEntry : g_mapCallTraces)
            pairEntry.second.Draw();
        ImGui::End();
#endif
	}

    void PostDraw() {
        // Moved to config to be grabbed by other methods instead of regrabbing again
        //auto vec2ScreenSize = ImGui::GetIO().DisplaySize;
        auto vec2Pos = ImVec2{ CheatConfig::Get().m_misc.vec2ScreenSize.x / 2.f + 10.f, CheatConfig::Get().m_misc.vec2ScreenSize.y / 2.f + 10.f };
        auto pDrawList = ImGui::GetBackgroundDrawList();

        #ifdef _DEBUG
        if(Cheat::g_bIsDesynced){
            pDrawList->AddText(ImVec2{ vec2Pos.x + 1.f, vec2Pos.y + 1.f }, IM_COL32(0, 0, 0, 255), "DESYNC");
            pDrawList->AddText(vec2Pos, IM_COL32(220, 0, 0, 255), "DESYNC");
            vec2Pos.y += 16.f;
        }

        if(Cheat::g_stTargetInfo){
            pDrawList->AddText(ImVec2{ vec2Pos.x + 1.f, vec2Pos.y + 1.f }, IM_COL32(0, 0, 0, 255), "Target");
            pDrawList->AddText(vec2Pos, IM_COL32(11, 220, 0, 255), "Target");
            vec2Pos.y += 16.f;
        }
        #endif
    }
}
