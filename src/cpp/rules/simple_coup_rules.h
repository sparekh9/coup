#pragma once

#include <cstdint>
#include <string>
#include <algorithm>

// Forward declarations
template<typename Rules> struct GameState;
template<typename Rules> struct ActionList;
template<typename Rules> void lose_influence(GameState<Rules>& state, int player_id);

// ============================================================================
// SIMPLE COUP RULES (Quick 1v1 variant)
// ============================================================================
// 1 influence per player (quick games!)
// 3 card types: Duke, Captain, Assassin
// 4 actions: Income, Tax, Steal, Coup
// Faster gameplay with cheaper coup and more starting coins
// ============================================================================

struct SimpleCoupRules {
    // ========================================================================
    // Type Definitions
    // ========================================================================

    enum class Influence : uint8_t {
        DUKE = 0,
        CAPTAIN = 1,
        ASSASSIN = 2
    };

    enum class Action : uint8_t {
        INCOME = 0,
        TAX = 1,
        STEAL = 2,
        COUP = 3  // No assassinate
    };

    // ========================================================================
    // Game Configuration Constants
    // ========================================================================

    static constexpr int MAX_INFLUENCES_PER_PLAYER = 1;  // Key difference!
    static constexpr int STARTING_INFLUENCES = 1;
    static constexpr int NUM_INFLUENCE_TYPES = 3;
    static constexpr int DECK_SIZE = 6;  // 2 of each
    static constexpr int STARTING_COINS = 2;  // More starting coins for faster games
    static constexpr int COUP_COST = 7;  // Cheaper coup
    static constexpr int ASSASSINATE_COST = 3;  // Not used
    static constexpr int MUST_COUP_THRESHOLD = 7;  // Lower threshold
    static constexpr int STEAL_AMOUNT = 2;
    static constexpr int TAX_AMOUNT = 3;
    static constexpr int INCOME_AMOUNT = 1;

    // ========================================================================
    // String Conversions
    // ========================================================================

    static std::string influence_to_string(Influence inf) {
        switch (inf) {
            case Influence::DUKE: return "DUKE";
            case Influence::CAPTAIN: return "CAPTAIN";
            case Influence::ASSASSIN: return "ASSASSIN";
            default: return "";
        }
    }

    static std::string action_to_string(Action act) {
        switch (act) {
            case Action::INCOME: return "INCOME";
            case Action::TAX: return "TAX";
            case Action::STEAL: return "STEAL";
            case Action::COUP: return "COUP";
            default: return "";
        }
    }

    // ========================================================================
    // Action Properties
    // ========================================================================

    static Influence get_required_influence(Action action) {
        switch (action) {
            case Action::TAX: return Influence::DUKE;
            case Action::STEAL: return Influence::CAPTAIN;
            default: return Influence::DUKE;
        }
    }

    static int get_action_cost(Action action) {
        return (action == Action::COUP) ? COUP_COST : 0;
    }

    static bool is_challengeable(Action action) {
        return action == Action::TAX || action == Action::STEAL;
    }

    static const char* get_variant_name() {
        return "SimpleCoup";
    }

    // ========================================================================
    // Custom Game Logic - Legal Actions
    // ========================================================================

    template<typename GameStateType>
    static void populate_legal_actions(const GameStateType& state,
                                       ActionList<SimpleCoupRules>& result) {
        result.count = 0;
        int coins = state.current_player == 1 ? state.p1_coins : state.p2_coins;

        // Force COUP at threshold
        if (coins >= MUST_COUP_THRESHOLD) {
            result.actions[result.count++] = Action::COUP;
            return;
        }

        // Always available actions
        result.actions[result.count++] = Action::INCOME;
        result.actions[result.count++] = Action::TAX;
        result.actions[result.count++] = Action::STEAL;

        // Coup available if we have enough coins
        if (coins >= COUP_COST) {
            result.actions[result.count++] = Action::COUP;
        }
    }

    // ========================================================================
    // Custom Game Logic - Execute Actions
    // ========================================================================

    template<typename GameStateType>
    static void execute_action(GameStateType& state, int player_id, Action action) {
        switch (action) {
            case Action::INCOME:
                // Already handled in apply_action
                break;

            case Action::TAX:
                if (player_id == 1) {
                    state.p1_coins += TAX_AMOUNT;
                } else {
                    state.p2_coins += TAX_AMOUNT;
                }
                break;

            case Action::STEAL: {
                if (player_id == 1) {
                    int steal_amt = std::min(STEAL_AMOUNT, (int)state.p2_coins);
                    state.p1_coins += steal_amt;
                    state.p2_coins -= steal_amt;
                } else {
                    int steal_amt = std::min(STEAL_AMOUNT, (int)state.p1_coins);
                    state.p2_coins += steal_amt;
                    state.p1_coins -= steal_amt;
                }
                break;
            }

            case Action::COUP:
                // Already handled in apply_action
                break;
        }
    }

    // ========================================================================
    // Challenge Logic Customization
    // ========================================================================

    template<typename GameStateType>
    static bool should_force_challenge(const GameStateType& state, Action pending_action) {
        const Influence* my_inf = (state.current_player == 1) ?
            state.p1_influences.data() : state.p2_influences.data();
        uint8_t my_inf_count = (state.current_player == 1) ?
            state.p1_influence_count : state.p2_influence_count;

        Influence required = get_required_influence(pending_action);

        // Count revealed copies
        int revealed_count_of_card = 0;
        for (uint8_t i = 0; i < state.revealed_count; i++) {
            if (state.revealed_cards[i] == required) {
                revealed_count_of_card++;
            }
        }

        // Count my copies (only 0 or 1 possible in this variant)
        int my_count_of_card = 0;
        for (uint8_t i = 0; i < my_inf_count; i++) {
            if (my_inf[i] == required) {
                my_count_of_card++;
            }
        }

        // With only 1 influence, challenge logic is simpler
        // Rule 1: All required cards revealed → only CHALLENGE
        if (revealed_count_of_card >= 2) {
            return true;
        }

        // Rule 2: I hold the card and one is revealed → only CHALLENGE
        if (my_count_of_card > 0 && revealed_count_of_card >= 1) {
            return true;
        }

        return false;
    }
};
