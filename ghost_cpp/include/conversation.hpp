#pragma once

#include <string>
#include <vector>
#include <deque>
#include <mutex>
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "json.hpp"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

struct Turn {
    std::string timestamp;
    std::string user_text;
    std::string user_image_b64;
    std::string assistant_text;

    nlohmann::json toJson() const;
    static Turn fromJson(const nlohmann::json& j);
};

class ConversationMemory {
public:
    ConversationMemory(int max_pairs = 10, bool persist = true);

    void setLimits(int max_pairs, bool persist);
    bool isEnabled() const;
    size_t size();

    void addTurn(const std::string& user_text, const std::string& user_image_b64, const std::string& assistant_text);
    void clear();
    std::vector<Turn> getTurns(bool include_images = false);

private:
    void save();
    void load();
    static std::string getContextFilePath();

    int m_max_pairs;
    bool m_persist;
    std::deque<Turn> m_turns;
    std::mutex m_mutex;
};
