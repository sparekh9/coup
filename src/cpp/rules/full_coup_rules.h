#pragma once

#include <cstdint>
#include <string>
#include <algorithm>

// Forward declarations
template<typename Rules> struct GameState;
template<typename Rules> struct ActionList;
template<typename Rules> void lose_influence(GameState<Rules>& state, int player_id);

// ============================================================================
// FULL COUP RULES (Complete game with all cards and actions)
// ============================================================================
// 2 influences per player
// 5 card types: Duke, Assassin, Captain, Ambassador, Contessa
// 7 actions: Income, Foreign Aid, Tax, Steal, Assassinate, Exchange, Coup
// Includes blocking mechanics (not fully implemented in this CFR version)
// ============================================================================

struct FullCoupRules {
    // ========================================================================
    // Type Definitions
    // ========================================================================

    enum class Influence : uint8_t {
        DUKE = 0,
        ASSASSIN = 1,
        CAPTAIN = 2,
        AMBASSADOR = 3,
        CONTESSA = 4
    };

    enum class Action : uint8_t {
        INCOME = 0,
        FOREIGN_AID = 1,
        TAX = 2,
        STEAL = 3,
        ASSASSINATE = 4,
        EXCHANGE = 5,
        COUP = 6
    };

    // ========================================================================
    // Game Configuration Constants
    // ========================================================================

    static constexpr int MAX_INFLUENCES_PER_PLAYER = 2;
    static constexpr int STARTING_INFLUENCES = 2;
    static constexpr int NUM_INFLUENCE_TYPES = 5;
    static constexpr int DECK_SIZE = 11;  // 15 total (3 of each) - 4 dealt = 11
    static constexpr int STARTING_COINS = 2;
    static constexpr int COUP_COST = 7;
    static constexpr int ASSASSINATE_COST = 3;
    static constexpr int MUST_COUP_THRESHOLD = 10;
    static constexpr int STEAL_AMOUNT = 2;
    static constexpr int TAX_AMOUNT = 3;
    static constexpr int INCOME_AMOUNT = 1;
    static constexpr int FOREIGN_AID_AMOUNT = 2;

    // ========================================================================
    // String Conversions
    // ========================================================================

    static std::string influence_to_string(Influence inf) {
        switch (inf) {
            case Influence::DUKE: return "DUKE";
            case Influence::ASSASSIN: return "ASSASSIN";
            case Influence::CAPTAIN: return "CAPTAIN";
            case Influence::AMBASSADOR: return "AMBASSADOR";
            case Influence::CONTESSA: return "CONTESSA";
            default: return "";
        }
    }

    static std::string action_to_string(Action act) {
        switch (act) {
            case Action::INCOME: return "INCOME";
            case Action::FOREIGN_AID: return "FOREIGN_AID";
            case Action::TAX: return "TAX";
            case Action::STEAL: return "STEAL";
            case Action::ASSASSINATE: return "ASSASSINATE";
            case Action::EXCHANGE: return "EXCHANGE";
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
            case Action::EXCHANGE: return Influence::AMBASSADOR;
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
               action == Action::ASSASSINATE ||
               action == Action::EXCHANGE;
    }

    static const char* get_variant_name() {
        return "FullCoup";
    }

    // ========================================================================
    // Blocking Properties (not supported in FullCoup yet)
    // ========================================================================

    static bool is_blockable(Action action) {
        return false;  // No blocking in FullCoup (yet)
    }

    static Influence get_blocking_influence(Action action) {
        return Influence::DUKE;  // Unused
    }

    template<typename GameStateType>
    static bool should_force_block(const GameStateType& state, Action pending_action) {
        return false;  // No blocking
    }

    template<typename GameStateType>
    static bool should_force_challenge_block(const GameStateType& state, Influence claimed_blocker) {
        return false;  // No blocking
    }

    // ========================================================================
    // Custom Game Logic - Legal Actions
    // ========================================================================

    template<typename GameStateType>
    static void populate_legal_actions(const GameStateType& state,
                                       ActionList<FullCoupRules>& result) {
        result.count = 0;
        int coins = state.current_player == 1 ? state.p1_coins : state.p2_coins;

        // Force COUP at threshold
        if (coins >= MUST_COUP_THRESHOLD) {
            result.actions[result.count++] = Action::COUP;
            return;
        }

        // Always available actions
        result.actions[result.count++] = Action::INCOME;
        result.actions[result.count++] = Action::FOREIGN_AID;
        result.actions[result.count++] = Action::TAX;
        result.actions[result.count++] = Action::STEAL;
        result.actions[result.count++] = Action::EXCHANGE;

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

            case Action::FOREIGN_AID:
                if (player_id == 1) {
                    state.p1_coins += FOREIGN_AID_AMOUNT;
                } else {
                    state.p2_coins += FOREIGN_AID_AMOUNT;
                }
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

            case Action::EXCHANGE:
                // Simplified exchange for CFR (deterministic)
                // In real game: draw 2 cards, choose which to keep
                // For CFR: just shuffle the current cards (simplified)
                // TODO: Implement full exchange logic if needed
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

        // Count revealed copies (3 of each type in full game)
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

        // Force challenge rules
        // Rule 1: One influence + Assassination → only CHALLENGE
        if (pending_action == Action::ASSASSINATE && my_inf_count == 1) {
            return true;
        }

        // Rule 2: All required cards are revealed → only CHALLENGE
        // (3 of each type in full game)
        if (revealed_count_of_card >= 3) {
            return true;
        }

        // Rule 3: I hold 2 copies and 1 is revealed → only CHALLENGE
        if (my_count_of_card == 2 && revealed_count_of_card >= 1) {
            return true;
        }

        return false;
    }
};
