#pragma once

#include "game_state.h"
#include <unordered_map>
#include <string>
#include <vector>
#include <random>

// ============================================================================
// Custom hash and equality for GameAction variant
// ============================================================================
template<typename Rules>
struct GameActionHash {
    using Action = typename Rules::Action;
    using GameAction = std::variant<Action, ChallengeResponse>;

    size_t operator()(const GameAction& action) const {
        if (std::holds_alternative<Action>(action)) {
            return std::hash<int>()(static_cast<int>(std::get<Action>(action)));
        } else {
            return std::hash<int>()(100 + static_cast<int>(std::get<ChallengeResponse>(action)));
        }
    }
};

template<typename Rules>
struct GameActionEqual {
    using Action = typename Rules::Action;
    using GameAction = std::variant<Action, ChallengeResponse>;

    bool operator()(const GameAction& a, const GameAction& b) const {
        if (a.index() != b.index()) return false;
        if (std::holds_alternative<Action>(a)) {
            return std::get<Action>(a) == std::get<Action>(b);
        } else {
            return std::get<ChallengeResponse>(a) == std::get<ChallengeResponse>(b);
        }
    }
};

// ============================================================================
// CFRTrainer - Templated CFR trainer
// ============================================================================
template<typename Rules>
class CFRTrainer {
public:
    using Action = typename Rules::Action;
    using GameAction = std::variant<Action, ChallengeResponse>;
    using ActionMap = std::unordered_map<GameAction, double, GameActionHash<Rules>, GameActionEqual<Rules>>;

private:
    // Storage: info_set_key (uint64_t hash) -> (GameAction -> regret/strategy value)
    std::unordered_map<uint64_t, ActionMap> regret_sum;

    // Running weighted average strategy (updated incrementally)
    std::unordered_map<uint64_t, ActionMap> avg_strategy;

    // Total weight accumulated per info set (for weighted average normalization)
    std::unordered_map<uint64_t, double> total_weight;

    // Track current iteration for linear weighting
    int current_iteration;

    // Number of Monte Carlo rollouts to perform when depth limit is reached
    int num_rollouts;

    // Maximum depth before switching to Monte Carlo rollouts
    int max_depth;

    // Debug tracking
    bool debug_enabled;
    int debug_iteration;
    std::unordered_map<uint64_t, int> state_visit_count;
    std::unordered_map<std::string, int> action_count;
    int max_debug_states;  // Limit how many states to print in detail
    int debug_state_counter;

public:
    CFRTrainer();

    // Configuration
    void set_rollout_count(int count) { num_rollouts = count; }
    void set_max_depth(int depth) { max_depth = depth; }
    int get_max_depth() const { return max_depth; }

    // Debug configuration
    void enable_debug(bool enabled = true) { debug_enabled = enabled; }
    void set_debug_iteration(int iter) { debug_iteration = iter; }
    void set_max_debug_states(int max_states) { max_debug_states = max_states; }
    void print_debug_statistics() const;
    void reset_debug_counters();

    // Get current strategy using Regret Matching+ (RM+)
    ActionMap get_strategy(uint64_t info_set_key,
                          const ActionList<Rules>& actions);

    // Get average strategy over all training iterations
    ActionMap get_average_strategy(uint64_t info_set_key,
                                   const ActionList<Rules>& actions);

    // Train for specified number of iterations
    void train(int iterations);

    // Core CFR algorithm - recursive traversal
    double cfr(GameState<Rules>& state, int traversing_player,
              double reach_p1, double reach_p2);

    // External Sampling MCCFR
    double cfr_external_sampling(GameState<Rules>& state, int traversing_player,
                                 double reach_p1, double reach_p2, double sample_prob);

    // Monte Carlo rollout from current state to terminal
    double rollout(GameState<Rules> state);

    // Persistence
    void save_strategy(const std::string& filename) const;
    void load_strategy(const std::string& filename);

    // Statistics
    size_t get_info_set_count() const { return avg_strategy.size(); }
};

// Include template implementations
#include "cfr_trainer.tpp"
