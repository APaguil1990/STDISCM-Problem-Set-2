#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <mutex>
#include <condition_variable>
#include <vector>

namespace lfg {

// Represents the lifecycle state of a dungeon instance. 
enum class InstanceStatus {
    Idle, 
    Waiting, 
    Running,
    Stopped
}; 

// Configuration values that control the LFG simulation. 
struct LFGConfig {
    std::size_t maxInstances{};
    int minClearTimeSeconds{};
    int maxClearTimeSeconds{};
}; 

// Stores the number of players currently available for each role.
struct PlayerCounts {
    std::size_t tanks{};
    std::size_t healers{};
    std::size_t dps{};
};

// Per-instance statistics collected during the simulation. 
struct InstanceStatistics {
    std::size_t id{};
    InstanceStatus status{InstanceStatus::Idle};
    std::size_t partiesServed{};
    std::size_t totalDungeonSeconds{};
}; 

// Snapshot of the final or current simulation results. 
struct SimulationSummary {
    std::size_t maximumPossibleParties{};
    std::size_t totalPartiesFormed{};
    PlayerCounts remainingPlayers{};
    std::vector<InstanceStatistics> instances;
};

// Converts and instance status into a human-readable string. 
[[nodiscard]] const char* toString(InstanceStatus status) noexcept;

// Coordinates player queues, dungeon workers, synchronization, and statistics.
class LFGSystem {
public:
    // Creates an LFG system using the supplied confiugration and initial players. 
    LFGSystem(LFGConfig config, PlayerCounts initialPlayers); 

    // Ensures all owned worker threads are shut down safely. 
    ~LFGSystem(); 

    // The system owns synchronization primitives and worker threads. 
    // so copying and moving are intentionally disabled. 
    LFGSystem(const LFGSystem&) = delete; 
    LFGSystem& operator=(const LFGSystem&) = delete;
    LFGSystem(LFGSystem&) = delete;
    LFGSystem& operator=(LFGSystem&&) = delete;

    // Starts all dungeon-instance worker threads. 
    void start(); 

    // Blocks until all possible parties have completed their dungeon runs. 
    void waitUntilComplete();

    // Request worker termination and joins all running threads. 
    // Safe to call more than once. 
    void shutdown() noexcept;

    // Returns the theoretical maximum number of parties from the initial players. 
    [[nodiscard]] std::size_t maximumPossibleParties() const noexcept;

    // Returns a thread-safe snapshot of simulation statistics and remaining players. 
    [[nodiscard]] SimulationSummary summary() const;

private:
    // Centralized thread-safe logger used by all worker threads. 
    class ThreadSafeLogger; 

    // Represents one independently executing dungeon instance. 
    class DungeonInstance; 

    // Result of atomically removing one valid party from the writing players. 
    struct PartyClaim {
        std::size_t partyId{};
        PlayerCounts remainingPlayers{};
    };

    // Checks whether at least 1 tank, 1 healer, and 3 DPS are available. 
    // stateMutex_ must already be locked by the caller. 
    [[nodiscard]] bool canFormPartyLocked() const noexcept;

    // Waits for work or shutdown, then atomically claims one complete party. 
    [[nodiscard]] std::optional<PartyClaim> waitAndClaimParty(DungeonInstance& instance); 

    // Records completion of a dungeon run and updates shared statistics. 
    void completeParty(DungeonInstance& instance, int dungeonSeconds);

    // Immutable simulation configuration.
    LFGConfig config_; 

    // Player counts that have not yet been assigned to a party. 
    PlayerCounts waitingPlayers_;

    // Maximum number of parties possible from the original player counts. 
    const std::size_t maximumPossibleParties_; 

    // Protects player counts, instance state, statistics, and shutdown state. 
    mutable std::mutex stateMutex_;

    // Wakes workers when a party may be available or shutdown is requested. 
    std::condition_variable workAvailableCv_; 

    // Wakes the main thread when simulation completion conditions may be satisfied. 
    std::condition_variable completionCv_; 

    // Shared simulation state protected by stateMutex_.
    std::size_t totalPartiesFormed_{0}; 
    std::size_t activeRuns_{0};
    bool started_{false}; 
    bool shutdownRequested_{false}; 

    // Separately protects start/shutdown operations and thread lifecycle changes. 
    std::mutex lifecycleMutex_; 

    // Prevents restarting the system after its workers have been permanently stopped. 
    bool permanentlyStopped_{false}; 

    // Shared thread-safe logging service. 
    std::unique_ptr<ThreadSafeLogger> logger_;

    // Owns all dungeon instances and, indirectly, their worker threads. 
    std::vector<std::unique_ptr<DungeonInstance>> instances_;
};

} // namespace lfg