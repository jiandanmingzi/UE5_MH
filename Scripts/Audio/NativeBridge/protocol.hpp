#pragma once
#include <charconv>
#include <cstdint>
#include <sstream>
#include <string>

struct AudioCommand {
    std::string nonce, verb, basename;
    std::uint64_t sequence = 0;
    double capture_time = 0;
};
inline bool audio_token(const std::string& value, std::size_t limit) {
    if (value.empty() || value.size() > limit) return false;
    for (unsigned char c : value)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}
inline bool parse_audio_command(const std::string& body, AudioCommand& out) {
    if (body.size() > 512 || body.size() < 5 || body.compare(body.size() - 5, 5, "\nEND\n") != 0) return false;
    std::istringstream input(body);
    std::string header, seq, clock, end, extra;
    AudioCommand parsed;
    if (!std::getline(input, header) || !std::getline(input, parsed.nonce) ||
        !std::getline(input, seq) || !std::getline(input, parsed.verb) ||
        !std::getline(input, parsed.basename) || !std::getline(input, clock) ||
        !std::getline(input, end) || std::getline(input, extra)) return false;
    if (header != "MHGZ_AUDIO_NATIVE_V1" || end != "END" || !audio_token(parsed.nonce, 96) ||
        !audio_token(parsed.basename, 80) || seq.empty()) return false;
    const auto number = std::from_chars(seq.data(), seq.data() + seq.size(), parsed.sequence);
    if (number.ec != std::errc() || number.ptr != seq.data() + seq.size() || parsed.sequence == 0) return false;
    if (parsed.verb != "PING" && parsed.verb != "START" && parsed.verb != "STOP") return false;
    // Strict decimal, no NaN/Inf, exponent or arbitrary extra content.
    if (clock.empty() || clock.size() > 30) return false;
    unsigned points = 0;
    for (char c : clock) if (c == '.') { if (++points > 1) return false; }
                         else if (c < '0' || c > '9') return false;
    try { parsed.capture_time = std::stod(clock); } catch (...) { return false; }
    if (!(parsed.capture_time >= 0 && parsed.capture_time < 86400 * 7)) return false;
    out = std::move(parsed);
    return true;
}
