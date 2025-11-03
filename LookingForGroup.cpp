#include <iostream> 
#include <thread> 
#include <mutex>
#include <condition_variable> 
#include <vector> 
#include <queue> 
#include <atomic> 
#include <random> 
#include <chrono> 
#include <string> 
#include <iomanip> 
#include <algorithm> 

class LFGSystem {
private: 
    // Synchronization primitives 
    std::mutex mtx; 
    std::condition_variable cv; 

    // Player queues 
    std::queue<int> tankQueue; 
    std::queue<int> healerQueue; 
    std::queue<int> dpsQueue; 

    // Instance management 
    struct Instance {
        int id; 
        std::string status; 
        int partiesServed; 
        int totalTimeServed; 
        bool active; 
        std::thread thread;

        Instance(int i) : id(i), status("empty"), partiesServed(0), totalTimeServed(0), active(false) {} 
    }; 

    std::vector<Instance> instances; 
    // std::vector<std::thread> instanceThreads; 

    // Statistics 
    std::atomic<int> totalPartiesFormed{0}; 
    std::atomic<bool> running{true}; 
    std::atomic<int> instancesWaiting{0};

    // Configuration 
    int maxInstances; 
    int t1, t2;

    // Random number generation 
    std::random_device rd; 
    std::mt19937 gen; 

public: 
    LFGSystem(int n, int minTime, int maxTime) 
        : maxInstances(n), t1(minTime), t2(maxTime), gen(rd()) {
        instances.reserve(maxInstances); 
        for (int i = 0; i < maxInstances; ++i) {
            instances.emplace_back(i + 1);
        }
    } 

    ~LFGSystem() {
        stop();
    } 

    // Add players to queues 
    void addPlayers(int tanks, int healers, int dps) {
        std::lock_guard<std::mutex> lock(mtx); 

        for (int i = 0; i < tanks; ++i) {
            tankQueue.push(1);
        } 

        for (int i = 0; i < healers; ++i) {
            healerQueue.push(1); 
        } 

        for (int i = 0; i < dps; ++i) {
            dpsQueue.push(1); 
        }

        std::cout << "Added " << tanks << " tanks, " << healers << " healers, " << dps << " DPS to queue.\n"; 
        cv.notify_all();
    }

    // Check if party can be formed 
    bool canFormParty() { 
        // return !tankQueue.empty() && !healerQueue.empty() && dpsQueue.size() >= 3; 
        return tankQueue.size() >= 1 && healerQueue.size() >= 1 && dpsQueue.size() >= 3;
    } 

    // Improved party formation with better distribution 
    bool tryFormParty(int instanceID) {
        std::unique_lock<std::mutex> lock(mtx); 

        // Use timed wait to prevent instances from starving one another 
        if (!cv.wait_for(lock, std::chrono::milliseconds(100), 
                        [this] { return canFormParty() && instancesWaiting > 0; })) {
            return false;
        } 

        if (!canFormParty() || !running.load()) {
            return false;
        } 

        // Remove players from queues to form party 
        tankQueue.pop(); 
        healerQueue.pop(); 
        for (int i = 0; i < 3; ++i) {
            dpsQueue.pop();
        } 

        // Update instance status 
        instances[instanceID].status = "active"; 
        instances[instanceID].active = true; 
        instances[instanceID].partiesServed++; 
        totalPartiesFormed++; 

        std::cout << "Instance " << (instanceID + 1) << " formed a party. "
                  << "Remaining - Tanks: " << tankQueue.size() 
                  << ", Healers: " << healerQueue.size() 
                  << ", DPS: " << dpsQueue.size() << "\n";
        
        return true;
    }

    // Instance thread function with improved synchronzation 
    void instanceWorker(int instanceId) {
        while (running.load()) {
            {
                std::lock_guard<std::mutex> lock(mtx); 
                instancesWaiting++; 
            } 

            if (tryFormParty(instanceId)) {
                // Successfully formed a party, run dungeon 
                runDungeon(instanceId); 

                // Small delay to give other instances a chance 
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            } else {
                // Couldn't form party, wait before trying 
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            } 

            {
                std::lock_guard<std::mutex> lock(mtx); 
                instancesWaiting--;
            }
        }
    }

    // Simulate dungeon run with random time 
    void runDungeon(int instanceId) {
        std::uniform_int_distribution<> dis(t1, t2); 
        int dungeonTime = dis(gen); 

        std::cout << "Instace " << (instanceId + 1) << " starting dungeon (estimated time: " << dungeonTime << "s)\n";

        // Simulate dungeon run time 
        std::this_thread::sleep_for(std::chrono::seconds(dungeonTime)); 

        // Update instance status 
        std::lock_guard<std::mutex> lock(mtx); 
        instances[instanceId].status = "empty"; 
        instances[instanceId].active = false; 
        instances[instanceId].totalTimeServed += dungeonTime; 

        std::cout << "Instance " << (instanceId + 1) << " completed dungeon in " << dungeonTime << "s\n";
        cv.notify_all(); 
    }

    // Start LFG system 
    void start() {
        for (int i = 0; i < maxInstances; ++i) {
            instances[i].thread = std::thread([this, i]() {
                instanceWorker(i);
            });
        }
    } 

    // Stop LFG system 
    void stop() {
        running.store(false); 
        cv.notify_all(); 

        for (auto& instance : instances) {
            if (instance.thread.joinable()) {
                instance.thread.join();
            }
        }
    }

    // Display current status 
    void displayStatus() {
        std::lock_guard<std::mutex> lock(mtx); 

        std::cout << "\n=== Current Instance Status ===\n"; 
        for (const auto& instance : instances) {
            std::cout << "Instance " << std::setw(2) << instance.id 
                      << ": " << std::setw(6) << instance.status 
                      << " | Parties served: " << std::setw(3) << instance.partiesServed 
                      << " | Total time: " << std::setw(4) << instance.totalTimeServed 
                      << "s\n";
        }

        std::cout << "\n=== Queue Status ===\n"; 
        std::cout << "Tanks in queue: " << tankQueue.size() << "\n"; 
        std::cout << "Healers in queue: " << healerQueue.size() << "\n"; 
        std::cout << "DPS in queue: " << dpsQueue.size() << "\n"; 
        std::cout << "Total parties formed: " << totalPartiesFormed.load() << "\n"; 
        std::cout << "Instances waiting for parties: " << instancesWaiting.load() << "\n";
    }

    // Wait for all current parties to complete 
    void waitForCompletion() {
        bool shouldWait; 
        do {
            std::this_thread::sleep_for(std::chrono::seconds(1)); 

            std::lock_guard<std::mutex> lock(mtx); 
            shouldWait = false; 

            // Check if any isntance is active 
            for (const auto& instance : instances) {
                if (instance.active) {
                    shouldWait = true; 
                    break;
                }
            } 

            // Check if more parties can be formed 
            if (!shouldWait && canFormParty()) {
                shouldWait = true;
            }
        } while(shouldWait);
    }

    // Get summary  statistics 
    void displaySummary() {
        std::lock_guard<std::mutex> lock(mtx); 

        std::cout << "\n=== Final Sumamry ===\n"; 

        int totalParties = 0; 
        int totalTime = 0;
        for (const auto& instance : instances) {
            std::cout << "Instance " << std::setw(2) << instance.id 
                      << ": " << std::setw(3) << instance.partiesServed << " parties, " 
                      << std::setw(4) << instance.totalTimeServed << " seconds total \n"; 
            
            totalParties += instance.partiesServed; 
            totalTime += instance.totalTimeServed;
        }

        std::cout << "System Total: " << totalParties << " parties, " << totalTime << " seconds\n";

        // Calculate distribution fairness 
        if (totalParties > 0) {
            double average = static_cast<double>(totalParties) / instances.size(); 
            double fairness = 0.0; 
            
            for (const auto& instance : instances) {
                double diff = instance.partiesServed - average; 
                fairness += diff * diff;
            } 
            fairness = 1.0 / (1.0 + std::sqrt(fairness / instances.size())); 
            std::cout << "Distribution fairness: " << std::fixed << std::setprecision(2) << (fairness * 100) << "%\n";
        }
    }

    // Get remaining players in queue 
    void getRemainingPlayers(int& tanks, int& healers, int& dps) {
        std::lock_guard<std::mutex> lock(mtx); 
        tanks = tankQueue.size(); 
        healers = healerQueue.size(); 
        dps = dpsQueue.size();
    }
};

int main() {
    std::cout << "=== LFG (Looking for Group) Dungeon Queuing System ===\n\n"; 

    // Get user input 
    int n, t, h, d, t1, t2; 

    std::cout << "Enter maximum number of concurrent instances (n): "; 
    std::cin >> n; 

    std::cout << "Enter number of tank players in queue (t): "; 
    std::cin >> t; 

    std::cout << "Enter number of healer players in queue (h): "; 
    std::cin >> h; 

    std::cout << "Enter a number of DPS players in queue (d): "; 
    std::cin >> d; 

    std::cout << "Enter a minimum dungeon clear time (t1): "; 
    std::cin >> t1; 

    std::cout << "Enter maximum dungeon clear time (t2): "; 
    std::cin >> t2;

    // Validate input 
    if (n <= 0 || t < 0 || h < 0 || d < 0 || t1 < 0 || t2 < t1) {
        std::cerr << "Invalid input parameters!\n"; 
        return 1;
    } 

    if (t2 > 15) {
        std::cout << "Note: t2 should be <= 15 for testing. Adjusting to 15.\n"; 
        t2 = 15;
    } 

    // Calculate maximum possible parties 
    int maxPossibleParties = std::min({t, h, d / 3}); 
    std::cout << "\nMaximum possible parties from input: " << maxPossibleParties << "\n";

    // Create and start LFG 
    LFGSystem lfgsystem(n, t1, t2); 
    std::cout << "\nStarting LFG system...\n"; 
    lfgsystem.start();

    // Add initial players 
    lfgsystem.addPlayers(t, h, d); 

    // Display initial status 
    lfgsystem.displayStatus(); 

    // Wait for all parties to complete 
    std::cout << "\nWaiting for all parties to complete...\n"; 
    lfgsystem.waitForCompletion(); 

    // Stop the system 
    lfgsystem.stop(); 

    // Display final status and summary 
    lfgsystem.displayStatus(); 
    lfgsystem.displaySummary(); 

    // Show remaining players (if any) 
    int remainingTanks, remainingHealers, remainingDPS; 
    lfgsystem.getRemainingPlayers(remainingTanks, remainingHealers, remainingDPS);

    if (remainingTanks > 0 || remainingHealers > 0 || remainingDPS > 0) {
        std::cout << "\nRemaining players in queue:\n"; 
        std::cout << "Tanks: " << remainingTanks << "\n"; 
        std::cout << "Healers: " << remainingHealers << "\n"; 
        std::cout << "DPS: " << remainingDPS << "\n";

        // Explain why parties could not be formed 
        if (remainingTanks == 0) {
            std::cout << "No more tanks available to form parties.\n"; 
        } else if (remainingHealers == 0) {
            std::cout << "no more healers available to form parties.\n"; 
        } else if (remainingDPS < 3) {
            std::cout << "Not enough DPS (" << remainingDPS << ") to form parties.\n";
        }
    }

    std::cout << "\nLFG system shutdown complete."; 

    return 0;
}