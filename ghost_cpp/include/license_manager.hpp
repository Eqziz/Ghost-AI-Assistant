#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

struct LicenseData {
    int64_t telegram_id = 0;
    int64_t expires_at = 0;
    std::string hwid_hash;

    bool isExpired() const;
    int64_t daysLeft() const;
    std::string expiresStr() const;
};

namespace LicenseManager {
    // Получение уникального HWID системы
    std::string getHWID();
    std::string getHWIDHash();

    // Проверка лицензионного ключа (формат payload_b64.hmac_hex)
    std::optional<LicenseData> verifyKey(const std::string& key);

    // Сохранение и загрузка лицензии (~/.ghost_ai_license.json)
    bool saveLicense(const std::string& key);
    std::optional<LicenseData> loadLicense();
    void clearLicense();

    std::string formatLicenseInfo(const LicenseData& data);

    // Внутренний секрет лицензий
    std::vector<uint8_t> getLicenseSecret();
}
