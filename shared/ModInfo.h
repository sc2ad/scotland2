#pragma once

extern "C" {
#ifdef __cplusplus
    /// @brief struct containing information about the mod
    struct ModInfo;
#else
    /// @brief struct containing information about the mod
    typedef struct ModInfo_t ModInfo;
#endif
    /// @brief set the id, the passed buffer is copied, so if it's allocated you need to clean it up
    /// @param modInfo modInfo instance pointer
    /// @param id id to set
    void ModInfo_set_id(ModInfo* modInfo, const char* id);

    /// @brief set the version, the passed buffer is copied, so if it's allocated you need to clean it up
    /// @param modInfo modInfo instance pointer
    /// @param version version to set
    void ModInfo_set_version(ModInfo* modInfo, const char* version);

    /// @brief set the author, the passed buffer is copied, so if it's allocated you need to clean it up
    /// @param modInfo modInfo instance pointer
    /// @param author author to set
    void ModInfo_set_author(ModInfo* modInfo, const char* author);

    /// @brief set the description, the passed buffer is copied, so if it's allocated you need to clean it up
    /// @param modInfo modInfo instance pointer
    /// @param description description to set
    void ModInfo_set_description(ModInfo* modInfo, const char* description);

    /// @brief set the user pointer on this particular modInfo
    /// @param modInfo modInfo instance pointer
    /// @param user user pointer to set
    void ModInfo_set_user(ModInfo* modInfo, void* user);

    /// @brief get the id of the modInfo
    /// @param modInfo modInfo instance pointer
    /// @return internal buffer containing the id
    const char* ModInfo_get_id(ModInfo* modInfo);

    /// @brief get the version of the modInfo
    /// @param modInfo modInfo instance pointer
    /// @return internal buffer containing the version
    const char* ModInfo_get_version(ModInfo* modInfo);

    /// @brief get the author of the modInfo
    /// @param modInfo modInfo instance pointer
    /// @return internal buffer containing the author
    const char* ModInfo_get_author(ModInfo* modInfo);

    /// @brief get the description of the modInfo
    /// @param modInfo modInfo instance pointer
    /// @return internal buffer containing the description
    const char* ModInfo_get_description(ModInfo* modInfo);

    /// @brief get the user pointer of the modInfo
    /// @param modInfo modInfo instance pointer
    /// @return user pointer
    void* ModInfo_get_user(ModInfo* modInfo);

}

#ifdef __cplusplus
#include <string>
#include <vector>
struct ModInfo {
    std::string id;
    std::string version;
    std::string author;
    std::string description;
    void* user;
};
#endif