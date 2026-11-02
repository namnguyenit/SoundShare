#pragma once
#include "soundshare/desktop_jobs.hpp"
#include <windows.h>
#include <memory>
#include <string>

namespace soundshare::desktop {
inline constexpr UINT reply_message=WM_APP+41;
struct Reply {
    Job job;
    Json data=Json::object(),error=Json::object();
    bool online{},restarted{};
};
class Client {
public:
    explicit Client(HWND target);
    ~Client();
    void submit(Job job);
    void stop();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
std::string default_profile_path();
std::string friendly_error(const Json& error);
} // namespace soundshare::desktop
