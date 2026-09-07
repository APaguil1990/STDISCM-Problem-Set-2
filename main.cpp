#include "LookingForGroup.hpp"

#include <algorithm> 
#include <cstddef> 
#include <iostream> 
#include <limits> 
#include <optional> 
#include <sstream> 
#include <string> 
#include <string_view> 

namespace {

// Reads a complete line and converts it to a valid int. 
// Re-prompts when the input is non-numeric or outside the int range. 
std::optional<int> readInteger(std::string_view prompt) {
    while (true) {
        std::cout << prompt; 
        std::string line; 

        // Return nullopt if the input stream ends unexpectedly.
        if (!std::getline(std::cin, line)) {
            return std::nullopt;
        } 

        std::istringstream input(line);
        long long value = 0;
        char extra = '\0';

        // Accept only one complete integer with no trailing characters. 
        if ((input >> value) && !(input >> extra) && 
            value >= std::numeric_limits<int>::min() && 
            value <= std::numeric_limits<int>::max()) {
                return static_cast<int>(value);
        }

        std::cout << "Invalid input. Please enter a whole number.\n";
    }
}

// Reads an integer and ensures it falls within the required range. 
std::optional<int> readValidatedInteger(std::string_view prompt, int minimum, int maximum, std::string_view requirement) {
    while (true) {
        const std::optional<int> value = readInteger(prompt);

        // Propagate end-of-input to the caller.
        if (!value) {
            return std::nullopt;
        } 

        if (*value >= minimum && *value <= maximum) {
            return value; 
        } 

        std::cout << "Invalid value." << requirement << '\n';
    }
}

// Displays final simulation statistics and remaining-player bottlenecks. 
void printSummary(const lfg::SimulationSummary& summary) {
    std::cout << "\n=== Final Summary ===\n";
    std::cout << "Maximum possible parties: " << summary.maximumPossibleParties << '\n'; 
    std::cout << "Total parties formed:      " << summary.totalPartiesFormed << '\n';
    std::cout << "\nPer-instance statistics:\n";

    // Track the smallest and largest workloads for distribution reporting. 
    std::size_t minimumServed = summary.instances.empty() ? 0U : std::numeric_limits<std::size_t>::max();
    std::size_t maximumServed = 0U;

    // Print statistics for each dungeon instance. 
    for (const auto& instance : summary.instances) {
        minimumServed = std::min(minimumServed, instance.partiesServed);
        maximumServed = std::max(maximumServed, instance.partiesServed); 

        std::cout << "  Instance " << instance.id 
                  << ": status=" << lfg::toString(instance.status) 
                  << ", parties=" << instance.partiesServed
                  << ", dungeon time=" << instance.totalDungeonSeconds << "s\n";
    }

    // Show the difference between the busiest and least-used instances. 
    if (!summary.instances.empty()) {
        std::cout << "Party distribution spread (max-min): " << (maximumServed - minimumServed) << '\n';
    } 

    // Display players who could not be assigned to another complete party. 
    std::cout << "\nRemaining players:\n";
    std::cout << "  Tanks:   " << summary.remainingPlayers.tanks << '\n';
    std::cout << "  Healers: " << summary.remainingPlayers.healers << '\n';
    std::cout << "  DPS:     " << summary.remainingPlayers.dps << '\n';
    
    std::cout << "\nWhy no additional party can be formed:\n";

    bool reportedBottleneck = false; 

    // Report every limiting role instead of stopping after the first one. 
    if (summary.remainingPlayers.tanks < 1U) {
        std::cout << "  - No more tanks are available.\n";
        reportedBottleneck = true;
    } 

    if (summary.remainingPlayers.healers < 1U) {
        std::cout << "  - No more healers are available.\n";
        reportedBottleneck = true;
    } 

    if (summary.remainingPlayers.dps < 3U) {
        std::cout << "  - Fewer than 3 DPS players remain.\n"; 
        reportedBottleneck = true;
    }

    // This case occurs when shutdown happens before the queue is exhausted. 
    if (!reportedBottleneck) {
        std::cout << "  - Shutdown was requested before the input pool was exhausted.\n";
    }
}

} // namespace 

int main() {
    std::cout << "=== LFG (Looking for Group) Dungeon Queuing System ===\n\n";

    // Read and validate the maximum number of concurrent dungeon instances.
    const auto instances = readValidatedInteger(
        "Maximum concurrent instances: ", 
        1, 
        std::numeric_limits<int>::max(), 
        "The instance count must be greater than 0."
    );

    if (!instances) {
        std::cerr << "Input ended before configuration was complete.\n"; 
        return 1;
    } 

    // Read and validate the number of Tank players. 
    const auto tanks = readValidatedInteger(
        "Tank players: ", 
        0, 
        std::numeric_limits<int>::max(),
        "The tank count must be non-negative."
    ); 

    if (!tanks) {
        std::cerr << "Input ended before configuration was complete.\n"; 
        return 1;
    }

    // Read and validate the number of Healer players. 
    const auto healers = readValidatedInteger(
        "Healer players: ", 
        0, 
        std::numeric_limits<int>::max(),
        "The healer count must be non-negative."
    );

    if (!healers) {
        std::cerr << "Input ended before configuration was complete.\n";
        return 1;
    } 

    // Read and validate the number of DPS players. 
    const auto dps = readValidatedInteger(
        "DPS players: ", 
        0, 
        std::numeric_limits<int>::max(),
        "The DPS count must be non-negative."
    );

    if (!dps) {
        std::cerr << "Input ended before configuration was complete.\n"; 
        return 1;
    } 

    // Dungeon clear times must remain within the assignment's 1-15 second range. 
    const auto minClearTime = readValidatedInteger(
        "Minimum dungeon clear time in seconds: ",
        1, 
        15, 
        "The minimum clear time must be between 1 and 15 seconds."
    );

    if (!minClearTime) {
        std::cerr << "Input ended before configuration was complete.\n"; 
        return 1;
    }

    // Validated both the allowed range and the relationship to the minimum time. 
    std::optional<int> maxClearTime;

    while (true) {
        maxClearTime = readValidatedInteger(
            "Maximum dungeon clear time in seconds: ",
            1, 
            15, 
            "The maximum clear time must be between 1 and 15 seconds."
        );

        if (!maxClearTime) {
            std::cerr << "Input ended before configuration was complete.\n"; 
            return 1;
        }

        if (*maxClearTime >= *minClearTime) {
            break;
        } 

        std::cout << "Invalid value. Maximum clear time must be >= minimum clear time.\n";
    }

    // Convert validated user input into the system's configuration structures.
    const lfg::LFGConfig config {
        static_cast<std::size_t>(*instances),
        *minClearTime,
        *maxClearTime
    };

    const lfg::PlayerCounts players {
        static_cast<std::size_t>(*tanks),
        static_cast<std::size_t>(*healers),
        static_cast<std::size_t>(*dps)
    }; 

    try {
        // Construct the simulation using validated configuration and player counts. 
        lfg::LFGSystem system(config, players);
        
        std::cout << "\nMaximum possible parties: " << system.maximumPossibleParties() << '\n';
        std::cout << "Starting dungeon instances...\n";

        // Start workers, wait for all possible parties to finish, 
        // then shut down and join all worker threads.
        system.start();
        system.waitUntilComplete();
        system.shutdown();

        // Display the final thread-safe simulation snapshot.
        printSummary(system.summary());
        
        std::cout << "\nLFG system shutdown complete.\n";
    } catch (const std::exception& error) {
        // Report configuration, lifecycel, or thread-related failures. 
        std::cerr << "Fatal error: " << error.what() << '\n';
        return 1;
    } 

    return 0;
}