#pragma once

#include <array>
#include <vector>
#include <string>
#include <cstdint>
#include <variant>


// Enums for game elements
enum class Influence : uint8_t {
    DUKE = 0,
    CAPTAIN = 1,
    ASSASSIN = 2
};

enum class Action : uint8_t {
    INCOME = 0,
    TAX = 1,
    STEAL = 2,
    ASSASSINATE = 3,
    COUP = 4
};

enum class ChallengeResponse : uint8_t {
    PASS = 0,
    CHALLENGE = 1
};

// Type alias for action variant
using GameAction = std::variant<Action, ChallengeResponse>;

// Fixed-size action list (avoids heap allocation)
struct ActionList {
    std::array<GameAction, 5> actions;
    uint8_t count;

    const GameAction* begin() const { return actions.data(); }
    const GameAction* end() const { return actions.data() + count; }
};

// Game state representation
struct GameState {
    // Using fixed-size arrays instead of vectors to avoid heap allocation
    std::array<Influence, 2> p1_influences;
    uint8_t p1_influence_count;
    std::array<Influence, 2> p2_influences;
    uint8_t p2_influence_count;
    uint8_t p1_coins;
    uint8_t p2_coins;
    uint8_t current_player;  // 1 or 2
    std::array<Influence, 6> deck;
    uint8_t deck_count;
    std::array<Influence, 4> revealed_cards;
    uint8_t revealed_count;

    // Pending action state (for challenge responses)
    bool has_pending_action;
    int pending_action_player;  // Only valid if has_pending_action is true
    Action pending_action_type; // Only valid if has_pending_action is true

    // Track opponent's claimed roles (for information sets)
    // 0xFF means no claim yet, otherwise stores Influence value
    uint8_t p1_last_claim;  // Last influence claimed by P1 (0xFF = none)
    uint8_t p2_last_claim;  // Last influence claimed by P2 (0xFF = none)

    int depth;

    // Constructor
    GameState();

    // Core game state queries
    bool is_terminal() const;
    float get_utility(int player) const;
    uint64_t get_info_set_key(int player) const;

    // Legal actions (returns fixed-size list to avoid heap allocation)
    ActionList get_legal_actions() const;
};

// Free functions for game logic
GameState create_initial_state();
GameState apply_action(GameState state, const GameAction& action);  // Pass by value for move semantics

// Helper functions
std::string influence_to_string(Influence inf);
std::string action_to_string(Action act);
std::string challenge_response_to_string(ChallengeResponse resp);
std::string game_action_to_string(const GameAction& action);
Influence get_required_influence(Action action);
void shuffle_deck(GameState& state);
void lose_influence(GameState& state, int player_id);
void execute_action(GameState& state, int player_id, Action action);

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

