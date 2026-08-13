#include "producer/PWD_plugin.h"
#include "PWD_NextUnit.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstring>
#include <atomic>

namespace pc {

struct PWDState {
    PWD_NextUnit generator;
    bool use_lower_alpha = false;
    bool use_upper_alpha = false;
    bool use_numeric = false;
    bool use_non_alpha = false;
    int max_pwd_len = 10;
    int seq = 0;
};

static PWDState* g_pwd_state = nullptr;
static std::atomic<bool> g_password_found{false};
static std::string g_found_password;
static std::atomic<bool> g_file_error{false};
static std::string g_file_error_msg;

void pwd_set_found(const std::string& password) {
    g_found_password = password;
    g_password_found = true;
}

void pwd_set_file_error(const std::string& msg) {
    g_file_error_msg = msg;
    g_file_error = true;
}

const std::string& pwd_get_found_password() {
    return g_found_password;
}

const std::string& pwd_get_file_error() {
    return g_file_error_msg;
}

pc::TestPlugin create_pwd_plugin() {
    pc::TestPlugin plugin;

    plugin.startup = [](const std::string& config_path, const nlohmann::json& resume_state) {
        if (g_pwd_state) delete g_pwd_state;
        g_pwd_state = new PWDState();

        if (!config_path.empty()) {
            std::ifstream file(config_path);
            if (file.is_open()) {
                try {
                    nlohmann::json cfg = nlohmann::json::parse(file);
                    g_pwd_state->use_lower_alpha = cfg.value("use_lower_alpha", false);
                    g_pwd_state->use_upper_alpha = cfg.value("use_upper_alpha", false);
                    g_pwd_state->use_numeric = cfg.value("use_numeric", false);
                    g_pwd_state->use_non_alpha = cfg.value("use_non_alpha", false);
                    g_pwd_state->max_pwd_len = cfg.value("max_password_length", 10);

                    g_pwd_state->generator.set_useLAlpha(g_pwd_state->use_lower_alpha);
                    g_pwd_state->generator.set_useUAlpha(g_pwd_state->use_upper_alpha);
                    g_pwd_state->generator.set_useNumeric(g_pwd_state->use_numeric);
                    g_pwd_state->generator.set_useNAlpha(g_pwd_state->use_non_alpha);

                    std::string starting_pwd = cfg.value("starting_password", "");
                    if (!starting_pwd.empty()) {
                        int start_len = static_cast<int>(starting_pwd.size());
                        if (start_len > g_pwd_state->max_pwd_len)
                            start_len = g_pwd_state->max_pwd_len;

                        g_pwd_state->generator.set_testPwdLen(start_len);

                        for (int i = 0; i < start_len; i++) {
                            char c = starting_pwd[i];
                            int idx = -1;

                            for (int j = 0; j < 26; j++) {
                                if (c == 'a' + j) { idx = j; break; }
                            }
                            if (idx == -1) {
                                for (int j = 0; j < 26; j++) {
                                    if (c == 'A' + j) { idx = 26 + j; break; }
                                }
                            }
                            if (idx == -1) {
                                for (int j = 0; j < 10; j++) {
                                    if (c == '0' + j) { idx = 52 + j; break; }
                                }
                            }
                            if (idx == -1) {
                                const char nonAlpha[] = { '~', '-', '=', '_', '$', '%', '+', '.', ';', ':', '[', '(', '{', '}', ')', ']', '?', '@', '!', '#', '`', '*', ',' };
                                for (int j = 0; j < 23; j++) {
                                    if (c == nonAlpha[j]) { idx = 62 + j; break; }
                                }
                            }

                            if (idx >= 0) {
                                g_pwd_state->generator.set_charIndicies(i, idx);
                            }
                        }

                        for (int i = start_len; i < 10; i++) {
                            g_pwd_state->generator.set_charIndicies(i, -1);
                        }

                        g_pwd_state->generator.setNext();
                    }

                    file.close();
                } catch (const std::exception& e) {
                    std::cerr << "[PWD_StartUp] Error parsing config: " << e.what() << "\n";
                }
            } else {
                std::cerr << "[PWD_StartUp] Cannot open config: " << config_path << "\n";
            }
        }

        if (!resume_state.empty()) {
            if (resume_state.contains("charIndicies")) {
                auto& ci = resume_state["charIndicies"];
                for (int i = 0; i < 10 && i < static_cast<int>(ci.size()); i++) {
                    g_pwd_state->generator.set_charIndicies(i, ci[i].get<int>());
                }
            }
            if (resume_state.contains("testPwdLen")) {
                g_pwd_state->generator.set_testPwdLen(resume_state["testPwdLen"].get<int>());
            }
            if (resume_state.contains("seq")) {
                g_pwd_state->seq = resume_state["seq"].get<int>();
            }
        }

        std::cout << "[PWD_StartUp] Configured: LA=" << g_pwd_state->use_lower_alpha
                  << " UA=" << g_pwd_state->use_upper_alpha
                  << " Num=" << g_pwd_state->use_numeric
                  << " NA=" << g_pwd_state->use_non_alpha
                  << " maxLen=" << g_pwd_state->max_pwd_len << "\n";
    };

    plugin.next_unit = [](WorkUnitMessage& out) {
        if (!g_pwd_state) return false;

        int ret = g_pwd_state->generator.setNext();
        if (ret != PERMUTE_SUCCESS) return false;

        g_pwd_state->seq++;

        char* pwd = g_pwd_state->generator.get_plainPassword();
        std::string pwd_str(pwd ? pwd : "");

        out.job = nlohmann::json::object();
        out.job["task"] = "PWD";
        out.job["password"] = pwd_str;
        out.job["indicies"] = g_pwd_state->generator.get_pwdAsIndicies();
        out.job["text"] = g_pwd_state->generator.get_pwdAsText();
        out.job["seq"] = g_pwd_state->seq;

        return true;
    };

    plugin.checkpoint = []() {
        nlohmann::json j;
        if (!g_pwd_state) return j;

        j["seq"] = g_pwd_state->seq;
        j["testPwdLen"] = g_pwd_state->generator.get_testPwdLen();

        nlohmann::json ci = nlohmann::json::array();
        for (int i = 0; i < 10; i++) {
            ci.push_back(0);
        }
        j["charIndicies"] = ci;

        return j;
    };

    plugin.exit_conditions = []() {
        return g_password_found.load() || g_file_error.load();
    };

    return plugin;
}

} // namespace pc
