#include <iostream> 
#include <thread> 
#include <condition_variable> 
#include <vector> 
#include <queue> 
#include <atomic> 
#include <random> 
#include <chrono> 
#include <string> 
#include <iomanip> 

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

        Instance(int i) : id(i), status("empty"), partiesServed(0), totalTimeServed(0), active(false) {} 
    }; 

    std::vector<Instance> instances; 
    std::vector<std::thread> instanceThreads; 

    // Statistics 
    std::atomic<int> totalPartiesFormed{0}; 
    std::atomic<bool> running{true}; 

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
        return !tankQueue.empty() && !healerQueue.empty() && dpsQueue.size() >= 3; 
    } 

    // Form a party and assign to instance 
    void formParty(int instanceId) { 
        std::unique_lock<std::mutex> lock(mtx); 

        // Wait until we can form a party or system is stopping 
        cv.wait(lock, [this] { return canFormParty() || !running.load(); }); 
        if (!running.load()) return; 

        // Rmeove pl;ayers from queues to form party 
        tankQueue.pop(); 
        healerQueue.pop(); 
        for (int i = 0; i < 3; ++i) {
            dpsQueue.pop();
        } 

        // Update instance status 
        instances[instanceId].status = "active"; 
        instances[instanceId].active = true; 
        instances[instanceId].partiesServed++; 
        totalPartiesFormed++; 

        std::cout << "Party formed and assigned to Instance " << (instanceId + 1) << "\n";
        lock.unlock();

        // Simulate dungeon run 
        runDungeon(instanceId);
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
        instanceThreads.reserve(maxInstances); 

        for (int i = 0; i < maxInstances; ++i) { 
            instanceThreads.emplace_back([this, i] () {
                while (running.load()) {
                    formParty(i); 
                    // small delay to prevent looping 
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            });
        }
    } 

    // Stop LFG system 
    void stop() {
        running.store(false); 
        cv.notify_all(); 

        for (auto& thread : instanceThreads) {
            if (thread.joinable()) {
                thread.join();
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
    }

    // Wait for all current parties to complete 
    void waitForCompletion() {
        bool hasActiveInstances; 

        do {
            std::this_thread::sleep_for(std::chrono::seconds(1)); 

            std::lock_guard<std::mutex> lock(mtx); 
            hasActiveInstances = false; 
            for (const auto& instance : instances) {
                if (instance.active) {
                    hasActiveInstances = true; 
                    break;
                }
            }

            // Check if parties can be formed 
            if (!hasActiveInstances && !canFormParty()) {
                break;
            }
        } while (hasActiveInstances || canFormParty()); 
    }

    // Get summary  statistics 
    void displaySummary() {
        std::lock_guard<std::mutex> lock(mtx); 

        std::cout << "\n=== Final Sumamry ===\n"; 
        for (const auto& instance : instances) {
            std::cout << "Instance " << std::setw(2) << instance.id 
                      << ": " << std::setw(3) << instance.partiesServed << " parties, " 
                      << std::setw(4) << instance.totalTimeServed << " seconds total \n"; 
        }

        int totalParties = 0; 
        int totalTime = 0; 
        for (const auto& instance : instances) {
            totalParties += instance.partiesServed; 
            totalTime += instance.totalTimeServed; 
        } 

        std::cout << "System Total: " << totalParties << " parties, " << totalTime << " seconds\n";
    }
};