#include "game_state.h"
#include <algorithm>
#include <sstream>
#include <random>
#include <string>
#include <cmath>  // For std::abs

// GameState Member Functions

GameState::GameState()
    : p1_influence_count(0), p2_influence_count(0),
      p1_coins(0), p2_coins(0), current_player(1),
      deck_count(0), revealed_count(0),
      has_pending_action(false), pending_action_player(0),
      pending_action_type(Action::INCOME),
      p1_last_claim(0xFF), p2_last_claim(0xFF),  // 0xFF = no claim yet
      depth(0) {
}

bool GameState::is_terminal() const {
    // True terminal: One player has no influences left
    if (p1_influence_count == 0 || p2_influence_count == 0) {
        return true;
    }

    // Smart Early Termination: Stop if one player has overwhelming advantage
    if constexpr (ENABLE_EARLY_TERMINATION) {
        // Only apply after minimum depth to allow for comebacks early on
        if (depth >= EARLY_TERM_MIN_DEPTH) {
            int p1_score = calculate_score(p1_influence_count, p1_coins);
            int p2_score = calculate_score(p2_influence_count, p2_coins);
            int score_diff = std::abs(p1_score - p2_score);

            // Terminate if advantage is overwhelming
            if (score_diff >= EARLY_TERM_SCORE_THRESHOLD) {
                return true;
            }

            // Additional heuristics for decided games:
            // Note: Players are FORCED to coup at 10+ coins!

            // 1. Opponent must coup (10+ coins) and we have 2 influences
            //    They can only eliminate 1 influence, we'll still have 1 left
            //    Then we can likely coup them back
            if (p1_influence_count == 2 && p2_influence_count == 1 && p1_coins >= 7) {
                // P1 has 2 influences and can coup, P2 only has 1 → P1 wins
                return true;
            }
            if (p2_influence_count == 2 && p1_influence_count == 1 && p2_coins >= 7) {
                // P2 has 2 influences and can coup, P1 only has 1 → P2 wins
                return true;
            }

            // 2. Both players at 1 influence, but one has must-coup coins (10+)
            //    The player with 10+ coins will be forced to coup and win immediately
            if (p1_influence_count == 1 && p2_influence_count == 1) {
                if (p1_coins >= 10 && p2_coins < 7) {
                    // P1 must coup next turn, P2 can't coup back → P1 wins
                    return true;
                }
                if (p2_coins >= 10 && p1_coins < 7) {
                    // P2 must coup next turn, P1 can't coup back → P2 wins
                    return true;
                }
            }
        }
    }

    return depth >= DEPTH_LIMIT;
}

float GameState::get_utility(int player) const {
    // True terminal: Someone has no influences
    if (p1_influence_count == 0)
        return player == 1 ? -1.0f : 1.0f;
    if (p2_influence_count == 0)
        return player == 1 ? 1.0f : -1.0f;

    // Check if early termination applies (overwhelming advantage)
    // If so, award FULL utility to the winner!
    if constexpr (ENABLE_EARLY_TERMINATION) {
        if (depth >= EARLY_TERM_MIN_DEPTH) {
            int p1_score = calculate_score(p1_influence_count, p1_coins);
            int p2_score = calculate_score(p2_influence_count, p2_coins);
            int score_diff = std::abs(p1_score - p2_score);

            // Overwhelming score advantage → full utility
            if (score_diff >= EARLY_TERM_SCORE_THRESHOLD) {
                // Determine winner and award full utility
                if (p1_score > p2_score) {
                    return player == 1 ? 1.0f : -1.0f;  // P1 wins
                } else {
                    return player == 1 ? -1.0f : 1.0f;  // P2 wins
                }
            }

            // Influence advantage with coup coins → full utility
            if (p1_influence_count == 2 && p2_influence_count == 1 && p1_coins >= 7) {
                return player == 1 ? 1.0f : -1.0f;  // P1 wins
            }
            if (p2_influence_count == 2 && p1_influence_count == 1 && p2_coins >= 7) {
                return player == 1 ? -1.0f : 1.0f;  // P2 wins
            }

            // Must-coup advantage (1v1) → full utility
            if (p1_influence_count == 1 && p2_influence_count == 1) {
                if (p1_coins >= 10 && p2_coins < 7) {
                    return player == 1 ? 1.0f : -1.0f;  // P1 wins (forced coup)
                }
                if (p2_coins >= 10 && p1_coins < 7) {
                    return player == 1 ? -1.0f : 1.0f;  // P2 wins (forced coup)
                }
            }
        }
    }

    // Depth limit reached (no early termination) → partial utility based on score
    if (depth >= DEPTH_LIMIT) {
        int p1_score = p1_influence_count * 20 + p1_coins;
        int p2_score = p2_influence_count * 20 + p2_coins;
        float result = (p1_score - p2_score) / 50.0f;
        return player == 1 ? result : -result;
    }

    // Not terminal yet
    return 0.0f;
}

uint64_t GameState::get_info_set_key(int player) const {
    // Create integer hash key from player's perspective
    // This replaces expensive string construction with bit manipulation

    const Influence* my_inf;
    uint8_t my_inf_count;
    uint8_t my_coins, opp_inf_count, opp_coins;
    uint8_t opp_last_claim;

    if (player == 1) {
        my_inf = p1_influences.data();
        my_inf_count = p1_influence_count;
        my_coins = p1_coins;
        opp_inf_count = p2_influence_count;
        opp_coins = p2_coins;
        opp_last_claim = p2_last_claim;
    } else {
        my_inf = p2_influences.data();
        my_inf_count = p2_influence_count;
        my_coins = p2_coins;
        opp_inf_count = p1_influence_count;
        opp_coins = p1_coins;
        opp_last_claim = p1_last_claim;
    }

    // Sort influences for canonical representation
    std::array<Influence, 2> my_inf_sorted;
    for (uint8_t i = 0; i < my_inf_count; i++) {
        my_inf_sorted[i] = my_inf[i];
    }
    std::sort(my_inf_sorted.begin(), my_inf_sorted.begin() + my_inf_count);

    // Sort revealed cards for canonical representation
    std::array<Influence, 4> revealed_sorted;
    for (uint8_t i = 0; i < revealed_count; i++) {
        revealed_sorted[i] = revealed_cards[i];
    }
    std::sort(revealed_sorted.begin(), revealed_sorted.begin() + revealed_count);

    // Build hash using bit packing and mixing
    uint64_t hash = 0;

    // Pack my influences (2 bits each, 4 bits total)
    hash = (hash << 4) | (my_inf_count > 0 ? static_cast<uint64_t>(my_inf_sorted[0]) : 3);
    hash = (hash << 4) | (my_inf_count > 1 ? static_cast<uint64_t>(my_inf_sorted[1]) : 3);

    // Pack coins with efficient abstraction
    // ASYMMETRIC mode (default): my_coins exact (5 bits), opp_coins abstracted (2 bits)
    // This gives precise strategies for OUR actions while reducing state space
    uint8_t my_coins_hash = abstract_my_coins(my_coins);
    uint8_t opp_coins_hash = abstract_opp_coins(opp_coins);

    // Determine bit width based on abstraction mode (compile-time optimization)
    if constexpr (ABSTRACTION_MODE == AbstractionMode::NONE ||
                  ABSTRACTION_MODE == AbstractionMode::ASYMMETRIC) {
        hash = (hash << 5) | static_cast<uint64_t>(my_coins_hash);   // Exact: 5 bits
    } else {
        hash = (hash << 2) | static_cast<uint64_t>(my_coins_hash);   // Abstracted: 2 bits
    }

    if constexpr (ABSTRACTION_MODE == AbstractionMode::NONE) {
        hash = (hash << 5) | static_cast<uint64_t>(opp_coins_hash);  // Exact: 5 bits
    } else {
        hash = (hash << 2) | static_cast<uint64_t>(opp_coins_hash);  // Abstracted: 2 bits
    }

    // Pack opponent influence count (2 bits)
    hash = (hash << 2) | static_cast<uint64_t>(opp_inf_count);

    // Pack revealed cards (2 bits each, up to 4 cards)
    hash = (hash << 3) | static_cast<uint64_t>(revealed_count);
    for (uint8_t i = 0; i < revealed_count && i < 4; i++) {
        hash = (hash << 2) | static_cast<uint64_t>(revealed_sorted[i]);
    }

    // Pack pending action info
    hash = (hash << 1) | (has_pending_action ? 1ULL : 0ULL);
    if (has_pending_action) {
        hash = (hash << 1) | static_cast<uint64_t>(pending_action_player - 1);
        hash = (hash << 2) | static_cast<uint64_t>(pending_action_type);
    }

    // Pack opponent's last claim (2 bits)
    // 0xFF -> 3 (no claim), 0 -> 0 (Duke), 1 -> 1 (Captain), 2 -> 2 (Assassin)
    uint64_t claim_bits = (opp_last_claim == 0xFF) ? 3ULL : static_cast<uint64_t>(opp_last_claim);
    hash = (hash << 2) | claim_bits;

    return hash;
}

ActionList GameState::get_legal_actions() const {
    ActionList result;
    result.count = 0;

    if (has_pending_action) {
        // High-Confidence Pruning for Challenge Responses
        // Get information about the current player (responder)
        const Influence* my_inf = (current_player == 1) ? p1_influences.data() : p2_influences.data();
        uint8_t my_inf_count = (current_player == 1) ? p1_influence_count : p2_influence_count;

        // Get required influence for the pending action
        Influence required = get_required_influence(pending_action_type);

        // Count how many copies of the required card are revealed
        int revealed_count_of_card = 0;
        for (uint8_t i = 0; i < revealed_count; i++) {
            if (revealed_cards[i] == required) {
                revealed_count_of_card++;
            }
        }

        // Count how many copies of the required card I hold
        int my_count_of_card = 0;
        for (uint8_t i = 0; i < my_inf_count; i++) {
            if (my_inf[i] == required) {
                my_count_of_card++;
            }
        }

        // Apply pruning rules
        bool must_challenge = false;

        // Rule 1: One influence + Assassination → only CHALLENGE
        if (pending_action_type == Action::ASSASSINATE && my_inf_count == 1) {
            must_challenge = true;
        }

        // Rule 2: All required cards are revealed → only CHALLENGE
        if (revealed_count_of_card == 2) {
            must_challenge = true;
        }

        // Rule 3: I hold both copies → only CHALLENGE
        if (my_count_of_card == 2) {
            must_challenge = true;
        }

        // Rule 4: One copy held + one revealed → only CHALLENGE
        if (my_count_of_card + revealed_count_of_card == 2) {
            must_challenge = true;
        }

        // Apply pruning
        if (must_challenge) {
            result.actions[result.count++] = ChallengeResponse::CHALLENGE;
        } else {
            result.actions[result.count++] = ChallengeResponse::PASS;
            result.actions[result.count++] = ChallengeResponse::CHALLENGE;
        }
    }
    else {
        int coins = current_player == 1 ? p1_coins : p2_coins;

        // Force COUP
        if (coins >= 10) {
            result.actions[result.count++] = Action::COUP;
            return result;
        }

        result.actions[result.count++] = Action::INCOME;
        result.actions[result.count++] = Action::TAX;
        result.actions[result.count++] = Action::STEAL;
        if (coins >= 3) result.actions[result.count++] = Action::ASSASSINATE;
        if (coins >= 7) result.actions[result.count++] = Action::COUP;
    }
    return result;
}

// ============================================================================
// Free Functions - Game Logic
// ============================================================================

GameState create_initial_state() {

    GameState state;

    // Initialize deck with all 6 cards
    state.deck[0] = Influence::DUKE;
    state.deck[1] = Influence::DUKE;
    state.deck[2] = Influence::CAPTAIN;
    state.deck[3] = Influence::CAPTAIN;
    state.deck[4] = Influence::ASSASSIN;
    state.deck[5] = Influence::ASSASSIN;
    state.deck_count = 6;

    shuffle_deck(state);

    // Deal 2 cards to each player
    for (int i = 0; i < 2; i++){
        state.deck_count--;
        Influence card1 = state.deck[state.deck_count];
        state.p1_influences[state.p1_influence_count++] = card1;

        state.deck_count--;
        Influence card2 = state.deck[state.deck_count];
        state.p2_influences[state.p2_influence_count++] = card2;
    }

    shuffle_deck(state);

    state.p1_coins = 2;
    state.p2_coins = 2;

    state.current_player = 1;

    state.has_pending_action = false;

    state.depth = 0;

    return state;
}

GameState apply_action(GameState state, const GameAction& action) {
    // Pass by value for move semantics - 'state' is our working copy
    state.depth += 1;

    // Handle challenge response
    if (std::holds_alternative<ChallengeResponse>(action)) {
        ChallengeResponse resp = std::get<ChallengeResponse>(action);
        int actor_id = state.pending_action_player;
        Action pending_act = state.pending_action_type;
        int challenger_id = state.current_player;

        if (resp == ChallengeResponse::PASS) {
            // No challenge - execute the action
            state.has_pending_action = false;
            execute_action(state, actor_id, pending_act);
            state.current_player = 3 - actor_id;  // Switch back to opponent
        }
        else if (resp == ChallengeResponse::CHALLENGE) {
            // Challenge! Check if actor has the required card
            state.has_pending_action = false;
            Influence required_card = get_required_influence(pending_act);

            Influence* actor_influences = (actor_id == 1) ? state.p1_influences.data() : state.p2_influences.data();
            uint8_t& actor_count = (actor_id == 1) ? state.p1_influence_count : state.p2_influence_count;

            // Check if actor has the required card
            int card_idx = -1;
            for (uint8_t i = 0; i < actor_count; i++) {
                if (actor_influences[i] == required_card) {
                    card_idx = i;
                    break;
                }
            }

            if (card_idx >= 0) {
                // Actor has card - challenge fails
                // Challenger loses influence
                lose_influence(state, challenger_id);

                // Actor exchanges the revealed card back into deck
                // Remove card from actor's hand
                for (uint8_t i = card_idx; i < actor_count - 1; i++) {
                    actor_influences[i] = actor_influences[i + 1];
                }
                actor_count--;

                // Add to deck and shuffle
                if (state.deck_count < 6) {
                    state.deck[state.deck_count++] = required_card;
                    shuffle_deck(state);
                    // Draw new card
                    state.deck_count--;
                    actor_influences[actor_count++] = state.deck[state.deck_count];
                }

                // Action still happens
                execute_action(state, actor_id, pending_act);
            } else {
                // Actor doesn't have card - challenge succeeds
                // Actor loses influence, action fails
                lose_influence(state, actor_id);
            }

            state.current_player = 3 - actor_id;
        }
    }
    // Handle normal action
    else {
        Action act = std::get<Action>(action);

        // COUP and INCOME can't be challenged, execute immediately
        if (act == Action::COUP) {
            if (state.current_player == 1) {
                state.p1_coins -= 7;
                lose_influence(state, 2);
            } else {
                state.p2_coins -= 7;
                lose_influence(state, 1);
            }
            state.current_player = 3 - state.current_player;
        }
        else if (act == Action::INCOME) {
            if (state.current_player == 1) {
                state.p1_coins += 1;
            } else {
                state.p2_coins += 1;
            }
            state.current_player = 3 - state.current_player;
        }
        // TAX, STEAL, and ASSASSINATE can be challenged - set pending
        else if (act == Action::TAX || act == Action::STEAL || act == Action::ASSASSINATE) {
            state.has_pending_action = true;
            state.pending_action_player = state.current_player;
            state.pending_action_type = act;

            // Record the claim made by this player
            Influence claimed = get_required_influence(act);
            if (state.current_player == 1) {
                state.p1_last_claim = static_cast<uint8_t>(claimed);
            } else {
                state.p2_last_claim = static_cast<uint8_t>(claimed);
            }

            state.current_player = 3 - state.current_player;  // Switch to responder
        }
    }
    return state;
}


// String Helper Functions

std::string influence_to_string(Influence inf) {
    switch (inf) {
        case Influence::DUKE: return "DUKE";
        case Influence::CAPTAIN: return "CAPTAIN";
        case Influence::ASSASSIN: return "ASSASSIN";
        default: return "";
    }
}

std::string action_to_string(Action act) {
    switch (act) {
        case Action::INCOME: return "INCOME";
        case Action::TAX: return "TAX";
        case Action::STEAL: return "STEAL";
        case Action::ASSASSINATE: return "ASSASSINATE";
        case Action::COUP: return "COUP";
        default: return "";
    }
}

std::string challenge_response_to_string(ChallengeResponse resp) {
    switch (resp) {
        case ChallengeResponse::PASS: return "PASS";
        case ChallengeResponse::CHALLENGE: return "CHALLENGE";
        default: return "";
    }
}

std::string game_action_to_string(const GameAction& action) {
    if (std::holds_alternative<Action>(action)) {
        return action_to_string(std::get<Action>(action));
    }
    else {
        return challenge_response_to_string(std::get<ChallengeResponse>(action));
    }
}

// Return the influence card required for an action
Influence get_required_influence(Action action) {
    // TAX requires DUKE
    if (action == Action::TAX) return Influence::DUKE;
    // STEAL requires CAPTAIN
    else if (action == Action::STEAL) return Influence::CAPTAIN;
    // ASSASSINATE requires ASSASSIN
    else if (action == Action::ASSASSINATE) return Influence::ASSASSIN;
    // INCOME and COUP don't require anything (but shouldn't call this)
    return Influence::DUKE;
}

// ============================================================================
// Additional Helper Functions
// ============================================================================

// Helper to lose an influence
void lose_influence(GameState& state, int player_id) {
    Influence* influences = (player_id == 1) ? state.p1_influences.data() : state.p2_influences.data();
    uint8_t& count = (player_id == 1) ? state.p1_influence_count : state.p2_influence_count;

    if (count == 0) {
        return;
    }

    // Deterministically select which influence to lose based on game state
    // This ensures consistent behavior across different runs and threads
    // Create seed from current game state
    uint64_t seed = state.depth;
    seed = (seed << 8) | state.p1_coins;
    seed = (seed << 8) | state.p2_coins;
    seed = (seed << 4) | state.p1_influence_count;
    seed = (seed << 4) | state.p2_influence_count;
    seed = (seed << 4) | state.revealed_count;
    seed = (seed << 1) | player_id;

    std::mt19937 gen(seed);
    std::uniform_int_distribution<int> dist(0, count - 1);
    int card_idx = dist(gen);

    // Remove selected influence and add to revealed cards
    Influence lost = influences[card_idx];

    // Shift remaining influences
    for (uint8_t i = card_idx; i < count - 1; i++) {
        influences[i] = influences[i + 1];
    }
    count--;

    // Add to revealed cards
    state.revealed_cards[state.revealed_count++] = lost;
}

// Helper to execute an action's effect (after passing challenge or for unchallenged actions)
void execute_action(GameState& state, int player_id, Action action) {
    if (action == Action::TAX) {
        if (player_id == 1) {
            state.p1_coins += 3;
        } else {
            state.p2_coins += 3;
        }
    }
    else if (action == Action::STEAL) {
        if (player_id == 1) {
            int steal_amount = std::min(2, (int)state.p2_coins);
            state.p1_coins += steal_amount;
            state.p2_coins -= steal_amount;
        } else {
            int steal_amount = std::min(2, (int)state.p1_coins);
            state.p2_coins += steal_amount;
            state.p1_coins -= steal_amount;
        }
    }
    else if (action == Action::ASSASSINATE) {
        // Cost 3 coins and make opponent lose influence
        if (player_id == 1) {
            state.p1_coins -= 3;
            lose_influence(state, 2);  // Player 2 loses influence
        } else {
            state.p2_coins -= 3;
            lose_influence(state, 1);  // Player 1 loses influence
        }
    }
}

// Helper to shuffle deck (for card exchange after successful challenge defense)
void shuffle_deck(GameState& state) {
    // Use thread_local static RNG to provide true randomness across iterations
    // while remaining thread-safe for parallel rollouts
    thread_local static std::mt19937 gen(std::random_device{}());

    std::shuffle(state.deck.begin(), state.deck.begin() + state.deck_count, gen);
}
