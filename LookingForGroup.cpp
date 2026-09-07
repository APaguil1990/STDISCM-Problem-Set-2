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

// Represents one independently executing dungeon worker. 
class LFGSystem::DungeonInstance {
public:
    // Creates an instance with its own thread-safe random-number engine. 
    DungeonInstance(std::size_t id, LFGSystem& owner) : id_(id), owner_(owner), rng_(makeRandomEngine(id)) {} 

    // RAII safeguard: never destroy an instance with a joinable thread.
    ~DungeonInstance() {
        join();
    } 

    // Instances own threads and therefore cannot be copied. 
    DungeonInstance(const DungeonInstance&) = delete; 
    DungeonInstance& operator=(const DungeonInstance&) = delete;

    // Launches the worker thread for this dungeon instance. 
    void start() {
        worker_ = std::thread(&DungeonInstance::workerLoop, this); 
    } 

    // Waits for the worker to finish if it was started. 
    void join() noexcept {
        if (worker_.joinable()) {
            worker_.join();
        }
    }

private: 
    // Main worker lifecycle: wait, claim a party, run the dungeon, and repeat. 
    void workerLoop() {
        std::uniform_int_distribution<int> clearTimeDistribution(
            owner_.config_.minClearTimeSeconds, 
            owner_.config_.maxClearTimeSeconds
        );

        while (true) {
            // Blocks until a complete party is available or shutdown is requested. 
            std::optional<PartyClaim> claim = owner_.waitAndClaimParty(*this);

            if (!claim) {
                return;
            } 

            // Log completed atomic party claim. 
            {
                std::ostringstream message; 
                message << "Instance " << id_ << " formed party " << claim->partyId
                        << ". Remaining - Tanks: " << claim->remainingPlayers.tanks
                        << ", Healers: " << claim->remainingPlayers.healers
                        << ", DPS: " << claim->remainingPlayers.dps;

                owner_.logger_->log(message.str());
            }

            // Generate the dungeon duration using this worker's private RNG.
            const int dungeonSeconds = clearTimeDistribution(rng_);

            {
                std::ostringstream message;
                message << "Instance " << id_ << " starting dungeon for party " 
                        << claim->partyId << " (estimated time: " << dungeonSeconds << "s)";

                owner_.logger_->log(message.str());
            }

            // Simulate the dungeon without holding the shared state mutex. 
            std::this_thread::sleep_for(std::chrono::seconds(dungeonSeconds)); 

            // Safely update shared completion statistics.
            owner_.completeParty(*this, dungeonSeconds); 

            {
                std::ostringstream message; 
                message << "Instance " << id_ << " completed party " << claim->partyId 
                        << " in " << dungeonSeconds << "s";

                owner_.logger_->log(message.str());
            }
        }
    }

    // Allows LFGSystem to safely inspect and update instance state. 
    friend class LFGSystem;

    // Unique instance identifier.
    std::size_t id_{};

    // Reference to the owning LFG system.
    LFGSystem& owner_;
    
    // Worker thread owned by this instance.
    std::thread worker_;

    // Private RNG prevents concurrent access to a shared random engine. 
    std::mt19937 rng_;

    // State and statistics protected by LFGSystem::stateMutex_.
    InstanceStatus status_{InstanceStatus::Idle};
    std::size_t partiesServed_{0};
    std::size_t totalDungeonSeconds_{0};
};

// Intializes the simulation and creates all dungeon-instance objects. 
LFGSystem::LFGSystem(LFGConfig config, PlayerCounts initialPlayers)
    : config_(config), waitingPlayers_(initialPlayers), 
    maximumPossibleParties_(calculateMaximumParties(initialPlayers)),
    logger_(std::make_unique<ThreadSafeLogger>()) { 

    // Reject invalid configuration before starting any threads. 
    validateConfiguration(config_); 
    instances_.reserve(config_.maxInstances);

    // Instance IDs are one-based for human-readable logging. 
    for (std::size_t i = 0; i < config_.maxInstances; i++) {
        instances_.push_back(std::make_unique<DungeonInstance>(i + 1U, *this));
    }
}

// Ensures all worker threads are stopped and joined before destruction. 
LFGSystem::~LFGSystem() {
    shutdown();
}

// Starts all dungeon-instance worker threads. 
void LFGSystem::start() {
    // Prevent concurrent start/shutdown operations. 
    std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_); 

    {
        std::lock_guard<std::mutex> stateLock(stateMutex_); 

        if (started_) {
            throw std::logic_error("LFGSystem::start() may only be called once");
        }

        if (permanentlyStopped_) {
            throw std::logic_error("LFGSystem cannot be restarted after shutdown");
        }

        started_ = true;
        shutdownRequested_ = false;
    }

    try {
        // Start each independently executing dungeon worker. 
        for (auto& instance : instances_) {
            instance->start();
        }
    } catch (...) {
        // If thread creation fails, request shutdown for any workers started. 
        {
            std::lock_guard<std::mutex> stateLock(stateMutex_); 
            shutdownRequested_ = true;
        } 

        workAvailableCv_.notify_all();

        // Join all successfully created threads before propagating the exception. 
        for (auto& instance : instances_) {
            instance->join();
        } 

        {
            std::lock_guard<std::mutex> stateLock(stateMutex_); 
            started_ = false; 
            permanentlyStopped_ = true; 
        } 

        throw;
    } 

    // Wake workers so they can immediately check for available parties. 
    workAvailableCv_.notify_all();
}

// Blocks until every possible party has finished processing. 
void LFGSystem::waitUntilComplete() {
    std::unique_lock<std::mutex> lock(stateMutex_);

    if (!started_) {
        throw std::logic_error("LFGSystem must be started before waiting for completion");
    } 

    // Sleep efficiently until normal completion or shutdown completion occurs. 
    completionCv_.wait(lock, [this] {
        const bool naturalCompletion = 
            totalPartiesFormed_ == maximumPossibleParties_ &&
            activeRuns_ == 0U;

        const bool shutdownCompletion =
            shutdownRequested_ && activeRuns_ == 0U;

        return naturalCompletion || shutdownCompletion;
    });
}

// Reqeusts shutdown and joins every worker thread. 
// The operation is idempotent. 
void LFGSystem::shutdown() noexcept {
    std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);

    {
        std::lock_guard<std::mutex> stateLock(stateMutex_);

        // A completed shutdown does not need to run again.
        if (permanentlyStopped_) {
            return;
        } 

        shutdownRequested_ = true;
    }

    // Wake workers and any thread waiting for simulation completion. 
    workAvailableCv_.notify_all();
    completionCv_.notify_all();

    // Wait for all active workers to finish and exit cleanly. 
    for (auto& instance : instances_) {
        instance->join();
    } 

    {
        std::lock_guard<std::mutex> stateLock(stateMutex_); 
        started_ = false; 
        permanentlyStopped_ = true;
    }
}

// Returns the maximum party count determined from the initial queue. 
std::size_t LFGSystem::maximumPossibleParties() const noexcept {
    return maximumPossibleParties_;
} 

// Returns a consistent snapshot of player and instance statistics. 
SimulationSummary LFGSystem::summary() const {
    std::lock_guard<std::mutex> lock(stateMutex_); 

    SimulationSummary result; 
    result.maximumPossibleParties = maximumPossibleParties_;
    result.totalPartiesFormed = totalPartiesFormed_;
    result.remainingPlayers = waitingPlayers_;

    result.instances.reserve(instances_.size()); 

    for (const auto& instance : instances_) {
        result.instances.push_back(InstanceStatistics{
            instance->id_,
            instance->status_,
            instance->partiesServed_,
            instance->totalDungeonSeconds_
        });
    }

    return result;
} 

// Checks whether the waiting players contain one complete party. 
// The caller must already hold stateMutex_.
bool LFGSystem::canFormPartyLocked() const noexcept {
    return waitingPlayers_.tanks >= 1U && waitingPlayers_.healers >= 1U 
           && waitingPlayers_.dps >= 3U;
} 

// Waits for available work and atomically claims one complete party. 
std::optional<LFGSystem::PartyClaim> LFGSystem::waitAndClaimParty(DungeonInstance& instance) {
    std::unique_lock<std::mutex> lock(stateMutex_); 

    // Stop immediately if shutdown was already requested. 
    if (shutdownRequested_) {
        instance.status_ = InstanceStatus::Stopped;
        return std::nullopt;
    } 

    instance.status_ = InstanceStatus::Waiting; 

    // Handles both normal wakeups and spurious wakeups through the predicate. 
    workAvailableCv_.wait(lock, [this] {
        return shutdownRequested_ || canFormPartyLocked();
    }); 

    if (shutdownRequested_) {
        instance.status_ = InstanceStatus::Stopped;
        return std::nullopt;
    }

    // Atomically remove exactly one Tank, one Healer, and three DPS. 
    --waitingPlayers_.tanks; 
    --waitingPlayers_.healers; 
    waitingPlayers_.dps -= 3U;

    // Record the newly formed and currently active party. 
    totalPartiesFormed_++;
    activeRuns_++;
    instance.partiesServed_++;
    instance.status_ = InstanceStatus::Running;

    // Return the claim together with a snapshot of remaining players. 
    return PartyClaim {
        totalPartiesFormed_,
        waitingPlayers_
    };
}

// Records completion of a dungeon run and wakes completion waiters. 
void LFGSystem::completeParty(DungeonInstance& instance, int dungeonSeconds) {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);

        instance.totalDungeonSeconds_ += static_cast<std::size_t>(dungeonSeconds);
        instance.status_ = InstanceStatus::Idle;

        activeRuns_--;
    } 

    // Notify the main thread that completion conditions may now be satisfied. 
    completionCv_.notify_all();
}

} // namespace lfg