#include "LookingForGroup.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace lfg {
namespace {

// Calculates the mamximum number of complete parties that can be formed. 
std::size_t calculateMaximumParties(const PlayerCounts& players) noexcept {
    return std::min( {players.tanks, players.healers, players.dps / 3U} );
}

// Creates an independent random-number engine for a dungeon instance. 
// Each worker owns its RNG, avoiding synchronization between threads. 
std::mt19937 makeRandomEngine(std::size_t instanceId) {
    std::random_device rd;

    // Include the instance ID in the seed to further differentiate workers. 
    const auto idLow = static_cast<unsigned int>(instanceId & 0xFFFFFFFFU);
    const auto idHigh = static_cast<unsigned int>((instanceId >> 32U) & 0xFFFFFFFFU);

    std::seed_seq seed {
        rd(), rd(), rd(), rd(), idLow, idHigh
    };

    return std::mt19937(seed);
}

// Validates simulation configuration before worker threads are created. 
void validateConfiguration(const LFGConfig& config) {
    if (config.maxInstances == 0u) {
        throw std::invalid_argument("maxInstances must be greater than zero");
    } 

    if (config.minClearTimeSeconds < 1 || config.minClearTimeSeconds > 15) {
        throw std::invalid_argument("minClearTimeSeconds must be between 1 and 15"); 
    } 

    if (config.maxClearTimeSeconds < 1 || config.maxClearTimeSeconds > 15) {
        throw std::invalid_argument("maxClearTimeSeconds must be between 1 and 15");
    } 

    if (config.maxClearTimeSeconds < config.minClearTimeSeconds) {
        throw std::invalid_argument("maxClearTimeSeconds must be >= minClearTimeSeconds");
    }
}

} // namespace

// Converts a strongly typed instance state into readable text. 
const char* toString(InstanceStatus status) noexcept {
    switch (status) {
        case InstanceStatus::Idle:
            return "Idle";
        case InstanceStatus::Waiting:
            return "Waiting";
        case InstanceStatus::Running:
            return "Running";
        case InstanceStatus::Stopped:
            return "Stopped";
    } 

    return "Unknown";
}

// Provides synchronized timestamped console output for all worker threads. 
class LFGSystem::ThreadSafeLogger {
public:
    // Serializes console access so messages from different threads do not interview. 
    void log(std::string_view message) {
        std::lock_guard<std::mutex> lock(outputMutex_);
        std::cout << '[' << timestampLocked() << "] " << message << '\n';
    }

private:
    // Produces a timestamp in HH:MM:SS.mmm format. 
    // Called while outputMutex_ is held, protecting std::localtime usage. 
    std::string timestampLocked() const {
        const auto now = std::chrono::system_clock::now();
        const auto timePointMilliseconds = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
        const auto milliseconds = timePointMilliseconds.time_since_epoch() % std::chrono::seconds(1); 
        const std::time_t rawTime = std::chrono::system_clock::to_time_t(now);
        std::tm localTime{};

        // Copy the returned time data while access is serialized by the logger mutex. 
        if (const std::tm* converted = std::localtime(&rawTime); converted != nullptr) {
            localTime = *converted;
        } 

        std::ostringstream out; 
        out << std::put_time(&localTime, "%H:%M:%S") << '-' 
            << std::setfill('0') << std::setw(3) << milliseconds.count();

        return out.str();
    }

    // Protects console output and timestamp conversion. 
    std::mutex outputMutex_;
};



} // namespace lfg