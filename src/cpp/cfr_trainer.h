#pragma once

#include "game_state.h"
#include <unordered_map>
#include <string>
#include <vector>
#include <random>
#include <utility>
#include <functional>  
#include <map>
#include <limits>

// ============================================================================
// Deal Enumeration - All possible initial card distributions
// ============================================================================
template<typename Rules>
struct Deal {
    GameState<Rules> state;
    double probability;
};

template<typename Rules>
std::vector<Deal<Rules>> enumerate_all_deals();

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
// Forward declaration for Best Response data structure
// ============================================================================
template<typename Rules>
struct BRInfoSetData;

// ============================================================================
// CFRTrainer - Discounted CFR (DCFR) Implementation
// ============================================================================
template<typename Rules>
class CFRTrainer {
public:
    using Action = typename Rules::Action;
    using GameAction = std::variant<Action, ChallengeResponse>;
    using ActionMap = std::unordered_map<GameAction, double, GameActionHash<Rules>, GameActionEqual<Rules>>;

private:
    std::unordered_map<uint64_t, ActionMap> regret_sum;
    std::unordered_map<uint64_t, ActionMap> avg_strategy;
    std::unordered_map<uint64_t, double> total_weight;

    int current_iteration;
    int max_depth;

    // DCFR parameters: α=1.5, β=0.0, γ=2.0 (empirically best)
    double dcfr_alpha;
    double dcfr_beta;
    double dcfr_gamma;

    // All possible initial deals (enumerated once at start)
    std::vector<Deal<Rules>> all_deals;

public:
    CFRTrainer();

    // Configuration
    void set_max_depth(int depth) { max_depth = depth; }
    int get_max_depth() const { return max_depth; }

    void set_dcfr_params(double alpha, double beta, double gamma) {
        dcfr_alpha = alpha;
        dcfr_beta = beta;
        dcfr_gamma = gamma;
    }

    // Core methods
    ActionMap get_strategy(uint64_t info_set_key, const ActionList<Rules>& actions);
    ActionMap get_average_strategy(uint64_t info_set_key, const ActionList<Rules>& actions);

    void train(int iterations, int exploitability_interval);

    double cfr(GameState<Rules>& state, int traversing_player,
               double reach_p1, double reach_p2);

    double cfr_external_sampling(GameState<Rules>& state, int traversing_player,
                                 double reach_p1, double reach_p2, double sample_prob);

    // Persistence
    void save_strategy(const std::string& filename) const;

    // Statistics
    size_t get_info_set_count() const { return avg_strategy.size(); }
    int get_current_iteration() const { return current_iteration; }

    // ========================================================================
    // Exploitability Computation (Information-Set Consistent Best Response)
    // ========================================================================
    double compute_exploitability();
    void save_convergence_data(const std::string& filename) const;
    
    // Convergence tracking
    std::vector<std::pair<int, double>> exploitability_history;
    
private:
    // Helper for DCFR discounts
    void apply_dcfr_discounts();
    double get_strategy_weight(double reach) const;

    // ========================================================================
    // Best Response Computation Helpers
    // ========================================================================
    double compute_best_response_value(int br_player);
};

// ============================================================================
// Include template implementations
// ============================================================================
#include "cfr_trainer.tpp"