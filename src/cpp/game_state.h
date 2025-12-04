#pragma once

#include "rules/game_rules.h"
#include <array>
#include <vector>
#include <string>
#include <cstdint>
#include <variant>
#include <cmath>

// ============================================================================
// Challenge Response (shared across all variants)
// Now also handles blocking to reduce game tree depth
// ============================================================================
enum class ChallengeResponse : uint8_t {
    PASS = 0,
    CHALLENGE = 1,
    BLOCK = 2  // Block the action (for blockable actions like STEAL, ASSASSINATE)
};

// ============================================================================
// Template Forward Declarations
// ============================================================================
template<typename Rules>
struct GameState;

template<typename Rules>
struct ActionList;

// ============================================================================
// ActionList - Fixed-size action list (avoids heap allocation)
// ============================================================================
template<typename Rules>
struct ActionList {
    using Action = typename Rules::Action;
    using GameAction = std::variant<Action, ChallengeResponse>;

    std::array<GameAction, 8> actions;  // Max 8 actions (enough for any variant)
    uint8_t count;

    const GameAction* begin() const { return actions.data(); }
    const GameAction* end() const { return actions.data() + count; }
};

// ============================================================================
// GameState - Templated game state representation
// ============================================================================
template<typename Rules>
struct GameState {
    using Influence = typename Rules::Influence;
    using Action = typename Rules::Action;
    using GameAction = std::variant<Action, ChallengeResponse>;

    // Fixed-size arrays based on Rules configuration
    std::array<Influence, Rules::MAX_INFLUENCES_PER_PLAYER> p1_influences;
    uint8_t p1_influence_count;
    std::array<Influence, Rules::MAX_INFLUENCES_PER_PLAYER> p2_influences;
    uint8_t p2_influence_count;
    uint8_t p1_coins;
    uint8_t p2_coins;
    uint8_t current_player;  // 1 or 2
    std::array<Influence, Rules::DECK_SIZE> deck;
    uint8_t deck_count;
    std::array<Influence, 2 * Rules::MAX_INFLUENCES_PER_PLAYER> revealed_cards;
    uint8_t revealed_count;

    // Pending action state (for challenge responses)
    bool has_pending_action;
    int pending_action_player;  // Only valid if has_pending_action is true
    Action pending_action_type; // Only valid if has_pending_action is true

    // Block challenge state (when someone blocks and gets challenged)
    bool has_pending_block_challenge;
    int pending_block_challenger;  // Who initiated the original action (can challenge the block)
    Influence pending_block_claim;  // What card the blocker claimed
    Action pending_block_action;    // What action was blocked (needed for execute_action if block fails)

    // Track claim history (for information sets)
    // Circular buffer: stores last 3 claims per player
    // Value 7 (0b111) means empty slot
    std::array<uint8_t, 3> p1_claim_history;  // Last 3 claims by P1
    std::array<uint8_t, 3> p2_claim_history;  // Last 3 claims by P2
    uint8_t p1_claim_count;  // Number of claims by P1 (0-3, wraps for circular buffer)
    uint8_t p2_claim_count;  // Number of claims by P2 (0-3, wraps for circular buffer)

    int depth;

    // Constructor
    GameState();

    // Core game state queries
    bool is_terminal() const;
    float get_utility(int player) const;
    uint64_t get_info_set_key(int player) const;

    // Legal actions (returns fixed-size list to avoid heap allocation)
    ActionList<Rules> get_legal_actions() const;
};

// ============================================================================
// Free Functions - Game Logic
// ============================================================================
template<typename Rules>
GameState<Rules> create_initial_state();

template<typename Rules>
GameState<Rules> apply_action(GameState<Rules> state,
                               const typename GameState<Rules>::GameAction& action);

// ============================================================================
// Helper Functions
// ============================================================================
std::string challenge_response_to_string(ChallengeResponse resp);

template<typename Rules>
std::string game_action_to_string(const typename GameState<Rules>::GameAction& action);

template<typename Rules>
void shuffle_deck(GameState<Rules>& state);

template<typename Rules>
void lose_influence(GameState<Rules>& state, int player_id);

template<typename Rules>
void execute_action(GameState<Rules>& state, int player_id,
                    typename Rules::Action action);

// ============================================================================
// EFFICIENT State Abstraction (Inline + Lookup Table)
// ============================================================================

// Configuration: Choose abstraction strategy at compile time

constexpr int DEPTH_LIMIT = 20;

enum class AbstractionMode {
    NONE,          // No abstraction - use exact coins (7hr runtime)
    ASYMMETRIC,    // RECOMMENDED: Exact my coins, abstract opponent (best balance)
    SYMMETRIC,     // Abstract both players (most aggressive, may hurt quality)
    FINE_GRAINED   // 5 buckets instead of 4 (slightly less reduction)
};

constexpr AbstractionMode ABSTRACTION_MODE = AbstractionMode::NONE;

// ============================================================================
// Smart Early Termination Configuration
// ============================================================================

// Enable/disable early termination (compile-time flag)
constexpr bool ENABLE_EARLY_TERMINATION = true;

// Early termination parameters
constexpr int EARLY_TERM_MIN_DEPTH = 10;     // Don't terminate before this depth
constexpr int EARLY_TERM_SCORE_THRESHOLD = 14;  // Score difference required for termination

// Scoring weights for early termination
constexpr int INFLUENCE_VALUE = 10;  // Each influence worth 10 points
constexpr int COIN_VALUE = 1;        // Each coin worth 1 point

// ============================================================================
// Utility Decay Configuration
// ============================================================================

// Global runtime configuration for utility decay
// These can be modified at runtime before training
struct UtilityDecayConfig {
    bool enabled = false;           // Enable/disable decay
    double alpha = 0.6;             // Decay strength (0-1)

    static UtilityDecayConfig& instance() {
        static UtilityDecayConfig config;
        return config;
    }
};

// Quadratic decay formula: utility × (1 - (α × depth / DEPTH_LIMIT)²)
inline double apply_quadratic_decay(double base_utility, int depth) {
    auto& config = UtilityDecayConfig::instance();
    if (!config.enabled) {
        return base_utility;
    }
    double normalized_depth = static_cast<double>(depth) / DEPTH_LIMIT;
    double decay_factor = 1.0 - std::pow(config.alpha * normalized_depth, 2.0);
    return base_utility * decay_factor;
}

// Helper function to calculate game score (for early termination)
inline int calculate_score(uint8_t influence_count, uint8_t coins) {
    return influence_count * INFLUENCE_VALUE + coins * COIN_VALUE;
}

// Inline abstraction functions - ZERO overhead!
// Uses lookup tables instead of branches for maximum speed

inline uint8_t abstract_coins_symmetric(uint8_t coins) {
    // 4 buckets based on available actions
    static constexpr uint8_t TABLE[32] = {
        0, 0, 0,                    // 0-2: Can't assassinate
        1, 1, 1, 1,                 // 3-6: Can assassinate, can't coup
        2, 2, 2,                    // 7-9: Can coup (optional)
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,  // 10-21: Must coup
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3         // 22-31: Must coup
    };
    return TABLE[coins];
}

inline uint8_t abstract_coins_opponent(uint8_t coins) {
    // Coarser abstraction for opponent (3 buckets)
    // We care less about opponent's exact coins
    static constexpr uint8_t TABLE[32] = {
        0, 0, 0, 0, 0, 0, 0,        // 0-6: Can't coup me
        1, 1, 1,                    // 7-9: Can coup me (optional)
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,  // 10-21: Must coup me
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2         // 22-31: Must coup me
    };
    return TABLE[coins];
}

inline uint8_t abstract_coins_fine(uint8_t coins) {
    // 5 buckets for finer-grained abstraction
    static constexpr uint8_t TABLE[32] = {
        0, 0, 0,                    // 0-2: Can't assassinate
        1, 1, 1, 1,                 // 3-6: Can assassinate
        2, 2, 2,                    // 7-9: Can coup (optional)
        3, 3, 3, 3, 3,              // 10-14: Must coup (low)
        4, 4, 4, 4, 4, 4, 4, 4, 4, 4,  // 15-24: Must coup (high)
        4, 4, 4, 4, 4, 4, 4         // 25-31: Must coup (high)
    };
    return TABLE[coins];
}

// Wrapper functions that dispatch based on compile-time mode
inline uint8_t abstract_my_coins(uint8_t coins) {
    if constexpr (ABSTRACTION_MODE == AbstractionMode::NONE) {
        return coins;  // No abstraction
    } else if constexpr (ABSTRACTION_MODE == AbstractionMode::ASYMMETRIC) {
        return coins;  // Keep my coins exact!
    } else if constexpr (ABSTRACTION_MODE == AbstractionMode::SYMMETRIC) {
        return abstract_coins_symmetric(coins);
    } else {  // FINE_GRAINED
        return abstract_coins_fine(coins);
    }
}

inline uint8_t abstract_opp_coins(uint8_t coins) {
    if constexpr (ABSTRACTION_MODE == AbstractionMode::NONE) {
        return coins;  // No abstraction
    } else if constexpr (ABSTRACTION_MODE == AbstractionMode::ASYMMETRIC) {
        return abstract_coins_opponent(coins);  // Coarse abstraction for opponent
    } else if constexpr (ABSTRACTION_MODE == AbstractionMode::SYMMETRIC) {
        return abstract_coins_symmetric(coins);
    } else {  // FINE_GRAINED
        return abstract_coins_fine(coins);
    }
}

// Include template implementations
#include "game_state.tpp"
