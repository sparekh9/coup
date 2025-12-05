#pragma once

#include <cstdint>
#include <string>
#include <algorithm>

// Forward declarations
template<typename Rules> struct GameState;
template<typename Rules> struct ActionList;
template<typename Rules> void lose_influence(GameState<Rules>& state, int player_id);

// ============================================================================
// SIMPLE COUP BLOCKING RULES (1v1 variant with Assassinate + Blocking)
// ============================================================================
// 1 influence per player (quick games!)
// 4 card types: Duke, Captain, Assassin, Contessa
// 5 actions: Income, Tax, Steal, Assassinate, Coup
// Blocking: Captain blocks Steal, Contessa blocks Assassinate
// ============================================================================

struct SimpleCoupBlockingRules {
    // ========================================================================
    // Type Definitions
    // ========================================================================

    enum class Influence : uint8_t {
        DUKE = 0,
        CAPTAIN = 1,
        ASSASSIN = 2,
        CONTESSA = 3
    };

    enum class Action : uint8_t {
        INCOME = 0,
        TAX = 1,
        STEAL = 2,
        ASSASSINATE = 3,
        COUP = 4
    };

    // ========================================================================
    // Game Configuration Constants
    // ========================================================================

    static constexpr int MAX_INFLUENCES_PER_PLAYER = 1;
    static constexpr int STARTING_INFLUENCES = 1;
    static constexpr int NUM_INFLUENCE_TYPES = 4;
    static constexpr int DECK_SIZE = 8;  // 2 of each (4 types × 2)
    static constexpr int STARTING_COINS = 2;
    static constexpr int COUP_COST = 7;
    static constexpr int ASSASSINATE_COST = 3;
    static constexpr int MUST_COUP_THRESHOLD = 7;
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
            case Influence::CONTESSA: return "CONTESSA";
            default: return "";
        }
    }

    static std::string action_to_string(Action act) {
        switch (act) {
            case Action::INCOME: return "INCOME";
            case Action::TAX: return "TAX";
            case Action::STEAL: return "STEAL";
            case Action::ASSASSINATE: return "ASSASSINATE";
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
            case Action::ASSASSINATE: return Influence::ASSASSIN;
            default: return Influence::DUKE;
        }
    }

    static int get_action_cost(Action action) {
        switch (action) {
            case Action::COUP: return COUP_COST;
            case Action::ASSASSINATE: return ASSASSINATE_COST;
            default: return 0;
        }
    }

    static bool is_challengeable(Action action) {
        return action == Action::TAX ||
               action == Action::STEAL ||
               action == Action::ASSASSINATE;
    }

    // ========================================================================
    // Blocking Properties
    // ========================================================================

    static bool is_blockable(Action action) {
        return action == Action::STEAL ||
               action == Action::ASSASSINATE;
    }

    static Influence get_blocking_influence(Action action) {
        switch (action) {
            case Action::STEAL: return Influence::CAPTAIN;
            case Action::ASSASSINATE: return Influence::CONTESSA;
            default: return Influence::DUKE;
        }
    }

    static const char* get_variant_name() {
        return "SimpleCoupBlocking";
    }

    // ========================================================================
    // Custom Game Logic - Legal Actions
    // ========================================================================

    template<typename GameStateType>
    static void populate_legal_actions(const GameStateType& state,
                                       ActionList<SimpleCoupBlockingRules>& result) {
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

        // Conditional actions based on coins
        if (coins >= ASSASSINATE_COST) {
            result.actions[result.count++] = Action::ASSASSINATE;
        }

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

            case Action::ASSASSINATE:
                // Cost already deducted when action was declared (in game_state.tpp)
                // Just handle the influence loss
                if (player_id == 1) {
                    lose_influence(state, 2);
                } else {
                    lose_influence(state, 1);
                }
                break;

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

        // Count my copies
        int my_count_of_card = 0;
        for (uint8_t i = 0; i < my_inf_count; i++) {
            if (my_inf[i] == required) {
                my_count_of_card++;
            }
        }

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

    // ========================================================================
    // Block Logic Customization
    // ========================================================================

    template<typename GameStateType>
    static bool should_force_block(const GameStateType& state, Action action) {
        // Force block if we have the blocking card
        // Contessa should always block Assassinate, Captain should always block Steal
        if (!is_blockable(action)) {
            return false;
        }

        Influence blocking_card = get_blocking_influence(action);

        const Influence* my_inf = (state.current_player == 1) ?
            state.p1_influences.data() : state.p2_influences.data();
        uint8_t my_inf_count = (state.current_player == 1) ?
            state.p1_influence_count : state.p2_influence_count;

        // Check if I have the blocking card
        for (uint8_t i = 0; i < my_inf_count; i++) {
            if (my_inf[i] == blocking_card) {
                return true;  // Force block
            }
        }

        return false;
    }

    template<typename GameStateType>
    static bool should_force_challenge_block(const GameStateType& state, Influence claimed_blocker) {
        // Reuse challenge logic for blocking
        const Influence* my_inf = (state.current_player == 1) ?
            state.p1_influences.data() : state.p2_influences.data();
        uint8_t my_inf_count = (state.current_player == 1) ?
            state.p1_influence_count : state.p2_influence_count;

        // Count revealed copies
        int revealed_count_of_card = 0;
        for (uint8_t i = 0; i < state.revealed_count; i++) {
            if (state.revealed_cards[i] == claimed_blocker) {
                revealed_count_of_card++;
            }
        }

        // Count my copies
        int my_count_of_card = 0;
        for (uint8_t i = 0; i < my_inf_count; i++) {
            if (my_inf[i] == claimed_blocker) {
                my_count_of_card++;
            }
        }

        // Force challenge-block rules

        // Rule 1: All blocking cards revealed → only CHALLENGE
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
