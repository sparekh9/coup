#pragma once

#include "game_state.h"
#include <unordered_map>
#include <string>
#include <vector>
#include <random>

// Custom hash for GameAction variant (needed for unordered_map key)
struct GameActionHash {
    size_t operator()(const GameAction& action) const {
        if (std::holds_alternative<Action>(action)) {
            return std::hash<int>()(static_cast<int>(std::get<Action>(action)));
        } else {
            return std::hash<int>()(100 + static_cast<int>(std::get<ChallengeResponse>(action)));
        }
    }
};

// Equality comparison for GameAction
struct GameActionEqual {
    bool operator()(const GameAction& a, const GameAction& b) const {
        if (a.index() != b.index()) return false;
        if (std::holds_alternative<Action>(a)) {
            return std::get<Action>(a) == std::get<Action>(b);
        } else {
            return std::get<ChallengeResponse>(a) == std::get<ChallengeResponse>(b);
        }
    }
};

class CFRTrainer {
private:
    // Storage: info_set_key (uint64_t hash) -> (GameAction -> regret/strategy value)
    // Using GameAction directly as key with custom hash
    std::unordered_map<uint64_t, std::unordered_map<GameAction, double, GameActionHash>> regret_sum;

    // Running weighted average strategy (updated incrementally)
    std::unordered_map<uint64_t, std::unordered_map<GameAction, double, GameActionHash>> avg_strategy;

    // Total weight accumulated per info set (for weighted average normalization)
    std::unordered_map<uint64_t, double> total_weight;

    // Track current iteration for linear weighting
    int current_iteration;

    // Number of Monte Carlo rollouts to perform when depth limit is reached
    int num_rollouts;

    // Maximum depth before switching to Monte Carlo rollouts
    int max_depth;

public:
    CFRTrainer();

    // Configuration
    void set_rollout_count(int count) { num_rollouts = count; }
    void set_max_depth(int depth) { max_depth = depth; }
    int get_max_depth() const { return max_depth; }

    // Get current strategy using Regret Matching+ (RM+)
    // Returns map from action to probability
    std::unordered_map<GameAction, double, GameActionHash> get_strategy(
        uint64_t info_set_key,
        const ActionList& actions);

    // Get average strategy over all training iterations
    std::unordered_map<GameAction, double, GameActionHash> get_average_strategy(
        uint64_t info_set_key,
        const ActionList& actions);

    // Train for specified number of iterations
    void train(int iterations);

    // Core CFR algorithm - recursive traversal
    // Returns utility for player 1 from this state
    double cfr(GameState& state, int traversing_player, double reach_p1, double reach_p2);

    // External Sampling MCCFR - used after max_depth
    // Explores all traversing player actions, samples opponent actions
    // sample_prob tracks the probability of the sampled path for importance sampling
    // Returns utility for player 1
    double cfr_external_sampling(GameState& state, int traversing_player,
                                 double reach_p1, double reach_p2, double sample_prob);

    // Monte Carlo rollout from current state to terminal
    // Samples actions according to current strategy until game ends
    // Returns utility for player 1
    double rollout(GameState state);

    // Persistence
    void save_strategy(const std::string& filename) const;
    void load_strategy(const std::string& filename);

    // Statistics
    size_t get_info_set_count() const { return avg_strategy.size(); }
};
