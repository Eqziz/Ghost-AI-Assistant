#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "conversation.hpp"
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace {
    std::wstring getContextFilePathW() {
        wchar_t userProfile[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, userProfile))) {
            return std::wstring(userProfile) + L"\\.ghost_ai_context.json";
        }
        return L".ghost_ai_context.json";
    }
}

std::string ConversationMemory::getContextFilePath() {
    std::wstring wp = getContextFilePathW();
    int len = WideCharToMultiByte(CP_UTF8, 0, wp.c_str(), -1, NULL, 0, NULL, NULL);
    if (len <= 1) return ".ghost_ai_context.json";
    std::string s(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wp.c_str(), -1, &s[0], len, NULL, NULL);
    return s;
}

nlohmann::json Turn::toJson() const {
    nlohmann::json j;
    j["timestamp"] = timestamp;
    j["user_text"] = user_text;
    j["user_image_b64"] = user_image_b64;
    j["assistant_text"] = assistant_text;
    return j;
}

Turn Turn::fromJson(const nlohmann::json& j) {
    Turn t;
    if (j.contains("timestamp")) t.timestamp = j["timestamp"].get<std::string>();
    if (j.contains("user_text")) t.user_text = j["user_text"].get<std::string>();
    if (j.contains("user_image_b64")) t.user_image_b64 = j["user_image_b64"].get<std::string>();
    if (j.contains("assistant_text")) t.assistant_text = j["assistant_text"].get<std::string>();
    return t;
}

ConversationMemory::ConversationMemory(int max_pairs, bool persist)
    : m_max_pairs((std::max)(0, max_pairs)), m_persist(persist)
{
    if (m_persist) {
        load();
    }
}

void ConversationMemory::setLimits(int max_pairs, bool persist) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_max_pairs = (std::max)(0, max_pairs);
    m_persist = persist;

    while (m_max_pairs > 0 && m_turns.size() > static_cast<size_t>(m_max_pairs)) {
        m_turns.pop_front();
    }
    if (m_max_pairs == 0) {
        m_turns.clear();
    }

    if (m_persist) {
        save();
    }
}

bool ConversationMemory::isEnabled() const {
    return m_max_pairs > 0;
}

size_t ConversationMemory::size() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_turns.size();
}

void ConversationMemory::addTurn(const std::string& user_text, const std::string& user_image_b64, const std::string& assistant_text) {
    if (!isEnabled()) return;

    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &t);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);

    Turn turn;
    turn.timestamp = buf;
    turn.user_text = user_text;
    turn.user_image_b64 = user_image_b64;
    turn.assistant_text = assistant_text;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_max_pairs > 0 && m_turns.size() >= static_cast<size_t>(m_max_pairs)) {
        m_turns.pop_front();
    }
    m_turns.push_back(turn);

    if (m_persist) {
        save();
    }
}

void ConversationMemory::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_turns.clear();
    if (m_persist) {
        save();
    }
}

std::vector<Turn> ConversationMemory::getTurns(bool include_images) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<Turn> result;
    for (const auto& t : m_turns) {
        Turn copy = t;
        if (!include_images) {
            copy.user_image_b64 = "";
        }
        result.push_back(copy);
    }
    return result;
}

void ConversationMemory::save() {
    try {
        nlohmann::json j;
        j["max_pairs"] = m_max_pairs;
        nlohmann::json turnsJson = nlohmann::json::array();
        for (const auto& t : m_turns) {
            turnsJson.push_back(t.toJson());
        }
        j["turns"] = turnsJson;

        std::wstring path = getContextFilePathW();
        std::ofstream ofs(path, std::ios::trunc);
        if (ofs.is_open()) {
            ofs << j.dump(2);
        }
    } catch (...) {}
}

void ConversationMemory::load() {
    if (m_max_pairs <= 0) return;
    std::wstring path = getContextFilePathW();
    std::ifstream ifs(path);
    if (!ifs.is_open()) return;

    try {
        nlohmann::json j;
        ifs >> j;
        if (j.contains("turns") && j["turns"].is_array()) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_turns.clear();
            for (const auto& item : j["turns"]) {
                m_turns.push_back(Turn::fromJson(item));
            }
            while (m_max_pairs > 0 && m_turns.size() > static_cast<size_t>(m_max_pairs)) {
                m_turns.pop_front();
            }
        }
    } catch (...) {}
}
