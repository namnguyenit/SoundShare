#pragma once
#include <nlohmann/json.hpp>
#include <memory>
#include <string>

namespace soundshare {
class Control {
public:
    Control();
    ~Control();
    nlohmann::json request(const std::string& line) noexcept;
    bool shutdown_requested() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
inline constexpr std::size_t max_message_bytes=65536;
} // namespace soundshare
