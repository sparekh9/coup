// Template implementations for GameState

#include <algorithm>
#include <sstream>
#include <random>
#include <string>
#include <iostream>

// ============================================================================
// GameState Member Functions
// ============================================================================

template<typename Rules>
GameState<Rules>::GameState()
    : p1_influence_count(0), p2_influence_count(0),
      p1_coins(0), p2_coins(0), current_player(1),
      deck_count(0), revealed_count(0),
      has_pending_action(false), pending_action_player(0),
      pending_action_type(static_cast<Action>(0)),
      has_pending_block_challenge(false), pending_block_challenger(0),
      pending_block_claim(static_cast<Influence>(0)),
      pending_block_action(static_cast<Action>(0)),
      p1_claim_history{7, 7, 7}, p2_claim_history{7, 7, 7},  // 7 = empty slot
      p1_claim_count(0), p2_claim_count(0),
      depth(0) {
}

template<typename Rules>
bool GameState<Rules>::is_terminal() const {
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

            // Additional heuristics for decided games (only for 2-influence variants)
            if constexpr (Rules::MAX_INFLUENCES_PER_PLAYER == 2) {
                // 1. Opponent must coup (10+ coins) and we have 2 influences
                if (p1_influence_count == 2 && p2_influence_count == 1 && p1_coins >= Rules::COUP_COST) {
                    return true;
                }
                if (p2_influence_count == 2 && p1_influence_count == 1 && p2_coins >= Rules::COUP_COST) {
                    return true;
                }

                // 2. Both players at 1 influence, but one has must-coup coins
                if (p1_influence_count == 1 && p2_influence_count == 1) {
                    if (p1_coins >= Rules::MUST_COUP_THRESHOLD && p2_coins < Rules::COUP_COST) {
                        return true;
                    }
                    if (p2_coins >= Rules::MUST_COUP_THRESHOLD && p1_coins < Rules::COUP_COST) {
                        return true;
                    }
                }
            }
        }
    }

    return depth >= DEPTH_LIMIT;
}

template<typename Rules>
float GameState<Rules>::get_utility(int player) const {
    // True terminal: Someone has no influences
    if (p1_influence_count == 0) {
        return player == 1 ? -1.0f : 1.0f;
    }
    if (p2_influence_count == 0) {
        return player == 1 ? 1.0f : -1.0f;
    }

    // Check if early termination applies (overwhelming advantage)
    if constexpr (ENABLE_EARLY_TERMINATION) {
        if (depth >= EARLY_TERM_MIN_DEPTH) {
            int p1_score = calculate_score(p1_influence_count, p1_coins);
            int p2_score = calculate_score(p2_influence_count, p2_coins);
            int score_diff = std::abs(p1_score - p2_score);

            // Overwhelming score advantage → full utility
            if (score_diff >= EARLY_TERM_SCORE_THRESHOLD) {
                if (p1_score > p2_score) {
                    return player == 1 ? 1.0f : -1.0f;  // P1 wins
                } else {
                    return player == 1 ? -1.0f : 1.0f;  // P2 wins
                }
            }

            // Early termination heuristics (only for 2-influence variants)
            if constexpr (Rules::MAX_INFLUENCES_PER_PLAYER == 2) {
                if (p1_influence_count == 2 && p2_influence_count == 1 && p1_coins >= Rules::COUP_COST) {
                    return player == 1 ? 1.0f : -1.0f;  // P1 wins
                }
                if (p2_influence_count == 2 && p1_influence_count == 1 && p2_coins >= Rules::COUP_COST) {
                    return player == 1 ? -1.0f : 1.0f;  // P2 wins
                }

                if (p1_influence_count == 1 && p2_influence_count == 1) {
                    if (p1_coins >= Rules::MUST_COUP_THRESHOLD && p2_coins < Rules::COUP_COST) {
                        return player == 1 ? 1.0f : -1.0f;
                    }
                    if (p2_coins >= Rules::MUST_COUP_THRESHOLD && p1_coins < Rules::COUP_COST) {
                        return player == 1 ? -1.0f : 1.0f;
                    }
                }
            }
        }
    }

    // Depth limit reached → partial utility based on score
    if (depth >= DEPTH_LIMIT) {
        int p1_score = p1_influence_count * 10 + p1_coins;
        int p2_score = p2_influence_count * 10 + p2_coins;
        float result = (p1_score - p2_score) / 30.0f;
        return player == 1 ? result : -result;
    }

    return 0.0f;
}

template<typename Rules>
uint64_t GameState<Rules>::get_info_set_key(int player) const {
    const Influence* my_inf;
    uint8_t my_inf_count;
    uint8_t my_coins, opp_inf_count, opp_coins;
    const uint8_t* my_claims;
    const uint8_t* opp_claims;
    uint8_t my_claim_cnt, opp_claim_cnt;

    if (player == 1) {
        my_inf = p1_influences.data();
        my_inf_count = p1_influence_count;
        my_coins = p1_coins;
        opp_inf_count = p2_influence_count;
        opp_coins = p2_coins;
        my_claims = p1_claim_history.data();
        opp_claims = p2_claim_history.data();
        my_claim_cnt = p1_claim_count;
        opp_claim_cnt = p2_claim_count;
    } else {
        my_inf = p2_influences.data();
        my_inf_count = p2_influence_count;
        my_coins = p2_coins;
        opp_inf_count = p1_influence_count;
        opp_coins = p1_coins;
        my_claims = p2_claim_history.data();
        opp_claims = p1_claim_history.data();
        my_claim_cnt = p2_claim_count;
        opp_claim_cnt = p1_claim_count;
    }

    // Sort influences for canonical representation
    std::array<Influence, Rules::MAX_INFLUENCES_PER_PLAYER> my_inf_sorted;
    for (uint8_t i = 0; i < my_inf_count; i++) {
        my_inf_sorted[i] = my_inf[i];
    }
    std::sort(my_inf_sorted.begin(), my_inf_sorted.begin() + my_inf_count);

    // Sort revealed cards for canonical representation
    std::array<Influence, 2 * Rules::MAX_INFLUENCES_PER_PLAYER> revealed_sorted;
    for (uint8_t i = 0; i < revealed_count; i++) {
        revealed_sorted[i] = revealed_cards[i];
    }
    std::sort(revealed_sorted.begin(), revealed_sorted.begin() + revealed_count);

    // Build hash using FIXED-WIDTH bit packing
    // Total: 53 bits (11 unused out of 64)
    uint64_t hash = 1;

    // Pack my influences (ALWAYS 2 slots of 3 bits, even for SimpleCoup)
    // Slots beyond MAX_INFLUENCES_PER_PLAYER are padded with 7
    for (int i = 0; i < 2; i++) {
        if (i < Rules::MAX_INFLUENCES_PER_PLAYER && i < my_inf_count) {
            hash = (hash << 3) | static_cast<uint64_t>(my_inf_sorted[i]);
        } else {
            hash = (hash << 3) | 7;  // Empty slot
        }
    }

    // Pack coins (4 bits for both players, no abstraction - exact coins)
    uint8_t my_coins_hash = abstract_my_coins(my_coins);
    uint8_t opp_coins_hash = abstract_opp_coins(opp_coins);
    hash = (hash << 4) | static_cast<uint64_t>(my_coins_hash);
    hash = (hash << 4) | static_cast<uint64_t>(opp_coins_hash);

    // Pack opponent influence count (2 bits)
    hash = (hash << 2) | static_cast<uint64_t>(opp_inf_count);

    // Pack revealed cards (ALWAYS 4 slots of 3 bits)
    // Unused slots are padded with 7
    for (int i = 0; i < 4; i++) {
        if (i < revealed_count) {
            hash = (hash << 3) | static_cast<uint64_t>(revealed_sorted[i]);
        } else {
            hash = (hash << 3) | 7;  // Empty slot
        }
    }

    // Pack pending state (4 bits: 1 bit state type + 3 bits action/claim)
    // State types: 0 = no pending/pending action, 1 = pending block challenge
    // This is optimized: BLOCK is now part of ChallengeResponse, not a separate state
    uint64_t pending_state_bits = 0;
    if (has_pending_block_challenge) {
        pending_state_bits = 1; // 1
        pending_state_bits = (pending_state_bits << 3) |
            static_cast<uint64_t>(pending_block_claim);
    } else if (has_pending_action) {
        pending_state_bits = 1; // 1
        pending_state_bits = (pending_state_bits << 3) |
            static_cast<uint64_t>(pending_action_type);
    } else {
        pending_state_bits = 0; // 0
        pending_state_bits = (pending_state_bits << 3) | 7;
    }
    hash = (hash << 4) | pending_state_bits;

    // Pack my claim history (3 slots of 3 bits each, 7 = empty)
    for (int i = 0; i < 3; i++) {
        hash = (hash << 3) | static_cast<uint64_t>(my_claims[i]);
    }

    // Pack opponent claim history (3 slots of 3 bits each, 7 = empty)
    for (int i = 0; i < 3; i++) {
        hash = (hash << 3) | static_cast<uint64_t>(opp_claims[i]);
    }

    // Pack my claim count (2 bits)
    hash = (hash << 2) | static_cast<uint64_t>(my_claim_cnt);

    // Pack opponent claim count (2 bits)
    hash = (hash << 2) | static_cast<uint64_t>(opp_claim_cnt);

    return hash;
}

template<typename Rules>
ActionList<Rules> GameState<Rules>::get_legal_actions() const {
    ActionList<Rules> result;
    result.count = 0;

    if (has_pending_block_challenge) {
        // Waiting for challenge-block decision
        bool must_challenge = Rules::should_force_challenge_block(*this, pending_block_claim);

        if (must_challenge) {
            result.actions[result.count++] = ChallengeResponse::CHALLENGE;
        } else {
            result.actions[result.count++] = ChallengeResponse::PASS;
            result.actions[result.count++] = ChallengeResponse::CHALLENGE;
        }
    }
    else if (has_pending_action) {
        // Merged challenge/block decision (optimization!)
        bool is_challengeable = Rules::is_challengeable(pending_action_type);
        bool is_blockable = Rules::is_blockable(pending_action_type);
        bool must_challenge = false;
        bool must_block = false;

        if (is_challengeable) {
            must_challenge = Rules::should_force_challenge(*this, pending_action_type);
        }

        if (is_blockable) {
            must_block = Rules::should_force_block(*this, pending_action_type);
        }

        if (must_challenge) {
            // Must challenge (all cards revealed)
            // NOTE: Challenge has priority over block - resolves game state faster
            result.actions[result.count++] = ChallengeResponse::CHALLENGE;
        } else if (must_block) {
            // Must block (we have the blocking card - Contessa for Assassinate, Captain for Steal)
            // NOTE: Only reached if must_challenge is false (challenge takes priority)
            result.actions[result.count++] = ChallengeResponse::BLOCK;
        } else if (is_blockable && !is_challengeable) {
            // Blockable but not challengeable (unused in current variants)
            result.actions[result.count++] = ChallengeResponse::PASS;
            result.actions[result.count++] = ChallengeResponse::BLOCK;
        } else if (is_blockable && is_challengeable) {
            // Both blockable and challengeable (e.g., STEAL, ASSASSINATE)
            result.actions[result.count++] = ChallengeResponse::PASS;
            result.actions[result.count++] = ChallengeResponse::CHALLENGE;
            result.actions[result.count++] = ChallengeResponse::BLOCK;
        } else {
            // Only challengeable (e.g., TAX)
            result.actions[result.count++] = ChallengeResponse::PASS;
            result.actions[result.count++] = ChallengeResponse::CHALLENGE;
        }
    }
    else {
        // Delegate action enumeration to Rules class
        Rules::populate_legal_actions(*this, result);
    }
    return result;
}

// ============================================================================
// Free Functions - Game Logic
// ============================================================================

template<typename Rules>
GameState<Rules> create_initial_state() {
    using Influence = typename Rules::Influence;

    GameState<Rules> state;

    // Initialize deck with cards based on NUM_INFLUENCE_TYPES
    // Assume 2 of each type for now (can be adjusted)
    state.deck_count = 0;
    for (int type = 0; type < Rules::NUM_INFLUENCE_TYPES; type++) {
        state.deck[state.deck_count++] = static_cast<Influence>(type);
        state.deck[state.deck_count++] = static_cast<Influence>(type);
    }

    shuffle_deck(state);

    // Deal STARTING_INFLUENCES to each player
    for (int i = 0; i < Rules::STARTING_INFLUENCES; i++) {
        state.deck_count--;
        Influence card1 = state.deck[state.deck_count];
        state.p1_influences[state.p1_influence_count++] = card1;

        state.deck_count--;
        Influence card2 = state.deck[state.deck_count];
        state.p2_influences[state.p2_influence_count++] = card2;
    }

    shuffle_deck(state);

    state.p1_coins = Rules::STARTING_COINS;
    state.p2_coins = Rules::STARTING_COINS;

    state.current_player = 1;

    state.has_pending_action = false;

    state.depth = 0;

    return state;
}

template<typename Rules>
GameState<Rules> apply_action(GameState<Rules> state,
                               const typename GameState<Rules>::GameAction& action) {
    using Action = typename Rules::Action;
    using Influence = typename Rules::Influence;

    state.depth += 1;

    // Handle challenge response
    if (std::holds_alternative<ChallengeResponse>(action)) {
        ChallengeResponse resp = std::get<ChallengeResponse>(action);

        // Check if this is a block challenge or action challenge
        if (state.has_pending_block_challenge) {
            // Challenge-block logic
            int blocker_id = 3 - state.pending_block_challenger;
            Influence claimed_card = state.pending_block_claim;

            if (resp == ChallengeResponse::PASS) {
                state.has_pending_block_challenge = false;
                // Block succeeds, action doesn't happen
                state.current_player = 3 - state.pending_block_challenger;
            }
            else if (resp == ChallengeResponse::CHALLENGE) {
                state.has_pending_block_challenge = false;

                Influence* blocker_influences = (blocker_id == 1) ?
                    state.p1_influences.data() : state.p2_influences.data();
                uint8_t& blocker_count = (blocker_id == 1) ?
                    state.p1_influence_count : state.p2_influence_count;

                // Check if blocker has the claimed card
                int card_idx = -1;
                for (uint8_t i = 0; i < blocker_count; i++) {
                    if (blocker_influences[i] == claimed_card) {
                        card_idx = i;
                        break;
                    }
                }

                if (card_idx >= 0) {
                    // Blocker has card - challenger loses influence
                    lose_influence(state, state.pending_block_challenger);

                    // Blocker exchanges card (same logic as action challenge)
                    for (uint8_t i = card_idx; i < blocker_count - 1; i++) {
                        blocker_influences[i] = blocker_influences[i + 1];
                    }
                    blocker_count--;

                    // Draw new card BEFORE adding revealed card back
                    if (state.deck_count > 0 && state.deck_count < Rules::DECK_SIZE) {
                        state.deck_count--;
                        blocker_influences[blocker_count++] = state.deck[state.deck_count];
                        state.deck[state.deck_count++] = claimed_card;
                        shuffle_deck(state);
                    }

                    // Block succeeds - action doesn't happen
                } else {
                    // Blocker doesn't have card - blocker loses influence
                    lose_influence(state, blocker_id);

                    // Action happens (if game not over)
                    if (!state.is_terminal()) {
                        execute_action(state, state.pending_block_challenger, state.pending_block_action);
                    }
                }

                state.current_player = 3 - state.pending_block_challenger;
            }
        }
        else if (state.has_pending_action) {
            int actor_id = state.pending_action_player;
            Action pending_act = state.pending_action_type;
            int challenger_id = state.current_player;

            if (resp == ChallengeResponse::PASS) {
                // PASS - let the action happen (optimization: no longer transition to block state)
                state.has_pending_action = false;
                execute_action(state, actor_id, pending_act);
                state.current_player = 3 - actor_id;
            }
        else if (resp == ChallengeResponse::CHALLENGE) {
            state.has_pending_action = false;
            Influence required_card = Rules::get_required_influence(pending_act);

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
                lose_influence(state, challenger_id);

                // Actor exchanges the revealed card back into deck
                for (uint8_t i = card_idx; i < actor_count - 1; i++) {
                    actor_influences[i] = actor_influences[i + 1];
                }
                actor_count--;

                // Draw new card BEFORE adding revealed card back (ensures different card)
                if (state.deck_count > 0 && state.deck_count < Rules::DECK_SIZE) {
                    // Draw new card first
                    state.deck_count--;
                    actor_influences[actor_count++] = state.deck[state.deck_count];

                    // Then add revealed card back to deck and shuffle
                    state.deck[state.deck_count++] = required_card;
                    shuffle_deck(state);
                }

                // Action still happens
                execute_action(state, actor_id, pending_act);
            } else {
                // Actor doesn't have card - challenge succeeds
                lose_influence(state, actor_id);
            }

            state.current_player = 3 - actor_id;
        }
        else if (resp == ChallengeResponse::BLOCK) {
            // BLOCK - claim to block the action (new optimization!)
            state.has_pending_action = false;

            if (!Rules::is_blockable(pending_act)) {
                // Error: tried to block unblockable action
                // This shouldn't happen if legal actions are correct
                // Just execute the action anyway
                execute_action(state, actor_id, pending_act);
                state.current_player = 3 - actor_id;
            } else {
                // Enter block challenge state
                state.has_pending_block_challenge = true;
                state.pending_block_challenger = actor_id;
                state.pending_block_claim = Rules::get_blocking_influence(pending_act);
                state.pending_block_action = pending_act;  // Save for potential execution if block fails

                // Record claim in history (blocker is current_player)
                Influence claimed = state.pending_block_claim;
                if (state.current_player == 1) {
                    uint8_t insert_pos = state.p1_claim_count % 3;
                    state.p1_claim_history[insert_pos] = static_cast<uint8_t>(claimed);
                    state.p1_claim_count++;
                } else {
                    uint8_t insert_pos = state.p2_claim_count % 3;
                    state.p2_claim_history[insert_pos] = static_cast<uint8_t>(claimed);
                    state.p2_claim_count++;
                }

                state.current_player = actor_id;  // Actor can challenge the block
            }
        }
        }
    }
    // Handle normal action
    else {
        Action act = std::get<Action>(action);

        // COUP - can't be challenged, execute immediately
        // Find COUP in the action enum (last action typically)
        if (Rules::get_action_cost(act) == Rules::COUP_COST) {
            if (state.current_player == 1) {
                state.p1_coins -= Rules::COUP_COST;
                lose_influence(state, 2);
            } else {
                state.p2_coins -= Rules::COUP_COST;
                lose_influence(state, 1);
            }
            state.current_player = 3 - state.current_player;
        }
        // INCOME - can't be challenged
        else if (static_cast<int>(act) == 0) {  // INCOME is always 0
            if (state.current_player == 1) {
                state.p1_coins += Rules::INCOME_AMOUNT;
            } else {
                state.p2_coins += Rules::INCOME_AMOUNT;
            }
            state.current_player = 3 - state.current_player;
        }
        // Challengeable actions
        else if (Rules::is_challengeable(act)) {
            // ASSASSINATE costs 3 coins upfront (even if blocked or challenged)
            if (Rules::get_action_cost(act) == Rules::ASSASSINATE_COST) {
                if (state.current_player == 1) {
                    state.p1_coins -= Rules::ASSASSINATE_COST;
                } else {
                    state.p2_coins -= Rules::ASSASSINATE_COST;
                }
            }

            state.has_pending_action = true;
            state.pending_action_player = state.current_player;
            state.pending_action_type = act;

            // Record the claim in circular buffer
            Influence claimed = Rules::get_required_influence(act);
            if (state.current_player == 1) {
                // Add to P1's claim history (circular buffer)
                uint8_t insert_pos = state.p1_claim_count % 3;
                state.p1_claim_history[insert_pos] = static_cast<uint8_t>(claimed);
                if (state.p1_claim_count < 3) {
                    state.p1_claim_count++;
                } else {
                    // Wrap around: we've filled all 3 slots, just update count
                    state.p1_claim_count++;
                }
            } else {
                // Add to P2's claim history (circular buffer)
                uint8_t insert_pos = state.p2_claim_count % 3;
                state.p2_claim_history[insert_pos] = static_cast<uint8_t>(claimed);
                if (state.p2_claim_count < 3) {
                    state.p2_claim_count++;
                } else {
                    // Wrap around: we've filled all 3 slots, just update count
                    state.p2_claim_count++;
                }
            }

            state.current_player = 3 - state.current_player;
        }
    }
    return state;
}

// ============================================================================
// Helper Functions
// ============================================================================

inline std::string challenge_response_to_string(ChallengeResponse resp) {
    switch (resp) {
        case ChallengeResponse::PASS: return "PASS";
        case ChallengeResponse::CHALLENGE: return "CHALLENGE";
        case ChallengeResponse::BLOCK: return "BLOCK";
        default: return "";
    }
}

template<typename Rules>
std::string game_action_to_string(const typename GameState<Rules>::GameAction& action) {
    if (std::holds_alternative<typename Rules::Action>(action)) {
        return Rules::action_to_string(std::get<typename Rules::Action>(action));
    } else {
        return challenge_response_to_string(std::get<ChallengeResponse>(action));
    }
}

template<typename Rules>
void shuffle_deck(GameState<Rules>& state) {
    thread_local static std::mt19937 gen(std::random_device{}());
    std::shuffle(state.deck.begin(), state.deck.begin() + state.deck_count, gen);
}

template<typename Rules>
void lose_influence(GameState<Rules>& state, int player_id) {
    using Influence = typename Rules::Influence;

    Influence* influences = (player_id == 1) ? state.p1_influences.data() : state.p2_influences.data();
    uint8_t& count = (player_id == 1) ? state.p1_influence_count : state.p2_influence_count;

    if (count == 0) {
        return;
    }

    // Deterministically select which influence to lose based on game state
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

template<typename Rules>
void execute_action(GameState<Rules>& state, int player_id,
                    typename Rules::Action action) {
    // Delegate execution to Rules class
    Rules::execute_action(state, player_id, action);
}
