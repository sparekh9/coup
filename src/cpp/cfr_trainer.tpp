// Template implementations for CFRTrainer (DCFR)

#include <algorithm>
#include <numeric>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <iomanip>
#include <chrono>
#include <functional>

// Format seconds as human-readable time
static std::string format_time(double seconds) {
    int hrs = static_cast<int>(seconds) / 3600;
    int mins = (static_cast<int>(seconds) % 3600) / 60;
    int secs = static_cast<int>(seconds) % 60;

    std::ostringstream oss;
    if (hrs > 0) {
        oss << hrs << "h " << mins << "m " << secs << "s";
    } else if (mins > 0) {
        oss << mins << "m " << secs << "s";
    } else {
        oss << secs << "s";
    }
    return oss.str();
}

// Progress bar with time estimation
static void print_progress(int current, int total, std::chrono::steady_clock::time_point start_time,
                           size_t info_set_count) {
    const int bar_width = 30;
    float progress = static_cast<float>(current) / total;
    int filled = static_cast<int>(bar_width * progress);

    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count() / 1000.0;
    double eta = (current > 0) ? elapsed / current * (total - current) : 0.0;
    double iters_per_sec = (elapsed > 0) ? current / elapsed : 0.0;

    std::cout << "\r[";
    for (int i = 0; i < bar_width; i++) {
        if (i < filled) std::cout << "=";
        else if (i == filled) std::cout << ">";
        else std::cout << " ";
    }
    std::cout << "] " << std::setw(3) << static_cast<int>(progress * 100) << "% "
              << current << "/" << total << " | "
              << std::fixed << std::setprecision(1) << iters_per_sec << " it/s | "
              << format_time(elapsed) << " elapsed | ETA: " << format_time(eta) << " | "
              << info_set_count << " info sets" << std::flush;

    if (current == total) {
        std::cout << "\n";
    }
}

// ============================================================================
// Deal Enumeration Implementation
// ============================================================================

// Helper: compute factorial
inline double factorial(int n) {
    double result = 1.0;
    for (int i = 2; i <= n; i++) result *= i;
    return result;
}

// Helper: compute multinomial coefficient for a multiset
inline double multinomial_coeff(const std::vector<int>& counts, int total) {
    double result = factorial(total);
    for (int c : counts) {
        result /= factorial(c);
    }
    return result;
}

template<typename Rules>
std::vector<Deal<Rules>> enumerate_all_deals() {
    using Influence = typename Rules::Influence;

    std::vector<Deal<Rules>> deals;

    constexpr int NUM_TYPES = Rules::NUM_INFLUENCE_TYPES;
    constexpr int COPIES_PER_TYPE = 2;  // Assume 2 copies of each card
    constexpr int TOTAL_CARDS = NUM_TYPES * COPIES_PER_TYPE;
    constexpr int P1_CARDS = Rules::STARTING_INFLUENCES;
    constexpr int P2_CARDS = Rules::STARTING_INFLUENCES;

    // Total ways to deal cards (for probability normalization)
    // = C(TOTAL_CARDS, P1_CARDS) * C(TOTAL_CARDS - P1_CARDS, P2_CARDS)
    double total_ways = 1.0;
    for (int i = 0; i < P1_CARDS; i++) {
        total_ways *= (TOTAL_CARDS - i);
        total_ways /= (i + 1);
    }
    for (int i = 0; i < P2_CARDS; i++) {
        total_ways *= (TOTAL_CARDS - P1_CARDS - i);
        total_ways /= (i + 1);
    }

    // Generate all possible hands for P1
    // A "hand" is a multiset of card types (e.g., {Duke, Duke} or {Duke, Captain})
    // We iterate over all combinations with repetition

    std::function<void(int, int, std::vector<Influence>&)> generate_hands;
    std::vector<std::vector<Influence>> p1_hands;

    generate_hands = [&](int cards_left, int min_type, std::vector<Influence>& current) {
        if (cards_left == 0) {
            p1_hands.push_back(current);
            return;
        }
        for (int t = min_type; t < NUM_TYPES; t++) {
            // Count how many of type t we already have
            int count = std::count(current.begin(), current.end(), static_cast<Influence>(t));
            if (count < COPIES_PER_TYPE) {
                current.push_back(static_cast<Influence>(t));
                generate_hands(cards_left - 1, t, current);
                current.pop_back();
            }
        }
    };

    std::vector<Influence> temp;
    generate_hands(P1_CARDS, 0, temp);

    // For each P1 hand, generate all compatible P2 hands
    for (const auto& p1_hand : p1_hands) {
        // Count cards used by P1
        std::vector<int> p1_counts(NUM_TYPES, 0);
        for (Influence inf : p1_hand) {
            p1_counts[static_cast<int>(inf)]++;
        }

        // Remaining cards available for P2
        std::vector<int> remaining(NUM_TYPES);
        for (int t = 0; t < NUM_TYPES; t++) {
            remaining[t] = COPIES_PER_TYPE - p1_counts[t];
        }

        // Generate P2 hands from remaining cards
        std::vector<std::vector<Influence>> p2_hands;

        std::function<void(int, int, std::vector<Influence>&)> generate_p2_hands;
        generate_p2_hands = [&](int cards_left, int min_type, std::vector<Influence>& current) {
            if (cards_left == 0) {
                p2_hands.push_back(current);
                return;
            }
            for (int t = min_type; t < NUM_TYPES; t++) {
                int count = std::count(current.begin(), current.end(), static_cast<Influence>(t));
                if (count < remaining[t]) {
                    current.push_back(static_cast<Influence>(t));
                    generate_p2_hands(cards_left - 1, t, current);
                    current.pop_back();
                }
            }
        };

        temp.clear();
        generate_p2_hands(P2_CARDS, 0, temp);

        for (const auto& p2_hand : p2_hands) {
            // Count cards used by P2
            std::vector<int> p2_counts(NUM_TYPES, 0);
            for (Influence inf : p2_hand) {
                p2_counts[static_cast<int>(inf)]++;
            }

            // Compute probability of this deal
            // Number of ways to get this specific (P1 hand, P2 hand) combination
            // = (ways to pick P1's cards) * (ways to pick P2's cards from remainder)

            // Ways to pick P1's hand = multinomial over card types
            double p1_ways = factorial(P1_CARDS);
            for (int t = 0; t < NUM_TYPES; t++) {
                // Choose p1_counts[t] cards from COPIES_PER_TYPE available
                for (int i = 0; i < p1_counts[t]; i++) {
                    p1_ways *= (COPIES_PER_TYPE - i);
                }
                p1_ways /= factorial(p1_counts[t]);
            }

            // Ways to pick P2's hand from remaining cards
            double p2_ways = factorial(P2_CARDS);
            for (int t = 0; t < NUM_TYPES; t++) {
                for (int i = 0; i < p2_counts[t]; i++) {
                    p2_ways *= (remaining[t] - i);
                }
                p2_ways /= factorial(p2_counts[t]);
            }

            double prob = (p1_ways * p2_ways) / total_ways;

            // Create the game state
            Deal<Rules> deal;
            deal.probability = prob;
            deal.state = GameState<Rules>();

            // Set up the deck (remaining cards after deal)
            deal.state.deck_count = 0;
            std::vector<int> deck_counts(NUM_TYPES);
            for (int t = 0; t < NUM_TYPES; t++) {
                deck_counts[t] = COPIES_PER_TYPE - p1_counts[t] - p2_counts[t];
                for (int i = 0; i < deck_counts[t]; i++) {
                    deal.state.deck[deal.state.deck_count++] = static_cast<Influence>(t);
                }
            }

            // Set P1's influences
            deal.state.p1_influence_count = 0;
            for (Influence inf : p1_hand) {
                deal.state.p1_influences[deal.state.p1_influence_count++] = inf;
            }

            // Set P2's influences
            deal.state.p2_influence_count = 0;
            for (Influence inf : p2_hand) {
                deal.state.p2_influences[deal.state.p2_influence_count++] = inf;
            }

            // Set other initial state
            deal.state.p1_coins = Rules::STARTING_COINS;
            deal.state.p2_coins = Rules::STARTING_COINS;
            deal.state.current_player = 1;
            deal.state.revealed_count = 0;
            deal.state.has_pending_action = false;
            deal.state.p1_last_claim = 0xFF;
            deal.state.p2_last_claim = 0xFF;
            deal.state.depth = 0;

            deals.push_back(deal);
        }
    }

    // Verify probabilities sum to 1
    double total_prob = 0.0;
    for (const auto& deal : deals) {
        total_prob += deal.probability;
    }

    // Normalize if there's floating point error
    if (std::abs(total_prob - 1.0) > 1e-9) {
        for (auto& deal : deals) {
            deal.probability /= total_prob;
        }
    }

    return deals;
}

// ============================================================================
// CFRTrainer Implementation (DCFR)
// ============================================================================

template<typename Rules>
CFRTrainer<Rules>::CFRTrainer()
    : current_iteration(0),
      max_depth(DEPTH_LIMIT),
      dcfr_alpha(1.5),
      dcfr_beta(0.0),
      dcfr_gamma(2.0) {

    // Enumerate all possible deals at construction time
    all_deals = enumerate_all_deals<Rules>();
    std::cout << "Enumerated " << all_deals.size() << " possible initial deals\n";
}

template<typename Rules>
typename CFRTrainer<Rules>::ActionMap CFRTrainer<Rules>::get_strategy(
    uint64_t info_set_key,
    const ActionList<Rules>& actions) {

    ActionMap strategy;
    int num_actions = actions.count;

    auto it = regret_sum.find(info_set_key);
    if (it != regret_sum.end()) {
        // Use positive regrets only for strategy computation
        double sum = 0.0;
        for (const auto& action : actions) {
            auto regret_it = it->second.find(action);
            double regret = (regret_it != it->second.end()) ? std::max(0.0, regret_it->second) : 0.0;
            strategy[action] = regret;
            sum += regret;
        }

        if (sum > 0.0) {
            for (auto& [_, prob] : strategy) {
                prob /= sum;
            }
        } else {
            for (auto& [_, prob] : strategy) {
                prob = 1.0 / num_actions;
            }
        }
    } else {
        for (const auto& action : actions) {
            strategy[action] = 1.0 / num_actions;
        }
    }

    return strategy;
}

template<typename Rules>
typename CFRTrainer<Rules>::ActionMap CFRTrainer<Rules>::get_average_strategy(
    uint64_t info_set_key,
    const ActionList<Rules>& actions) {

    auto it = avg_strategy.find(info_set_key);
    if (it != avg_strategy.end() && !it->second.empty()) {
        return it->second;
    }

    ActionMap uniform;
    double uniform_prob = 1.0 / actions.count;
    for (const auto& action : actions) {
        uniform[action] = uniform_prob;
    }
    return uniform;
}

template<typename Rules>
double CFRTrainer<Rules>::get_strategy_weight(double reach) const {
    return std::pow(current_iteration, dcfr_gamma) * reach;
}

template<typename Rules>
void CFRTrainer<Rules>::apply_dcfr_discounts() {
    double t = static_cast<double>(current_iteration);
    double t_alpha = std::pow(t, dcfr_alpha);
    double t_beta = std::pow(t, dcfr_beta);
    double pos_discount = t_alpha / (t_alpha + 1.0);
    double neg_discount = t_beta / (t_beta + 1.0);

    for (auto& [info_set_key, action_map] : regret_sum) {
        for (auto& [action, regret] : action_map) {
            regret *= (regret > 0) ? pos_discount : neg_discount;
        }
    }

    double t_gamma = std::pow(t, dcfr_gamma);
    double weight_discount = t_gamma / (t_gamma + 1.0);
    for (auto& [info_set_key, weight] : total_weight) {
        weight *= weight_discount;
    }
}

template<typename Rules>
void CFRTrainer<Rules>::train(int iterations, int exploit_interval, int exploit_samples) {
    std::cout << "Training DCFR for " << iterations << " iterations "
              << "(alpha=" << dcfr_alpha << ", beta=" << dcfr_beta << ", gamma=" << dcfr_gamma << ")\n";
    std::cout << "Enumerating over " << all_deals.size() << " possible deals per iteration\n";

    if (exploit_interval > 0) {
        std::cout << "Tracking exploitability every " << exploit_interval << " iterations "
                  << "(" << exploit_samples << " samples each)\n";
        convergence_data.clear();
    }

    auto start_time = std::chrono::steady_clock::now();

    for (int i = 0; i < iterations; i++) {
        current_iteration++;

        // Iterate over ALL possible initial deals, weighted by probability
        for (const auto& deal : all_deals) {
            // The deal probability is incorporated into the reach probabilities
            // This ensures regrets are properly weighted by chance probability
            double chance_prob = deal.probability;

            // Traverse from both players' perspectives
            GameState<Rules> state1 = deal.state;
            cfr(state1, 1, chance_prob, chance_prob);

            GameState<Rules> state2 = deal.state;
            cfr(state2, 2, chance_prob, chance_prob);
        }

        // Apply DCFR discounts
        apply_dcfr_discounts();

        // Exploitability tracking
        if (exploit_interval > 0 && current_iteration % exploit_interval == 0) {
            double exploit = estimate_exploitability_quiet(exploit_samples);
            convergence_data.push_back({current_iteration, exploit, get_info_set_count()});
            std::cout << "\n  [iter " << current_iteration << "] exploitability: "
                      << std::fixed << std::setprecision(4) << exploit << "\n";
        }

        // Progress update
        if (current_iteration % std::max(1, iterations / 100) == 0 || current_iteration == iterations) {
            print_progress(current_iteration, iterations, start_time, get_info_set_count());
        }
    }

    std::cout << "Done.\n";
}

template<typename Rules>
double CFRTrainer<Rules>::cfr(GameState<Rules>& state, int traversing_player,
                              double reach_p1, double reach_p2) {

    if (state.is_terminal()) {
        return state.get_utility(1);
    }

    if (state.depth >= max_depth) {
        return cfr_external_sampling(state, traversing_player, reach_p1, reach_p2, 1.0);
    }

    int current_player = state.current_player;
    uint64_t info_set_key = state.get_info_set_key(current_player);
    ActionList<Rules> actions = state.get_legal_actions();

    auto strategy = get_strategy(info_set_key, actions);

    ActionMap action_utilities;
    double expected_utility = 0.0;

    for (const auto& action : actions) {
        GameState<Rules> next_state = apply_action(state, action);

        double utility;
        if (current_player == 1) {
            utility = cfr(next_state, traversing_player, reach_p1 * strategy[action], reach_p2);
        } else {
            utility = cfr(next_state, traversing_player, reach_p1, reach_p2 * strategy[action]);
        }

        action_utilities[action] = utility;
        expected_utility += strategy[action] * utility;
    }

    // Update regrets and strategy for traversing player only
    if (current_player == traversing_player) {
        double opponent_reach = (current_player == 1) ? reach_p2 : reach_p1;

        for (const auto& action : actions) {
            double utility_for_player = (current_player == 1) ?
                action_utilities[action] : -action_utilities[action];
            double expected_for_player = (current_player == 1) ?
                expected_utility : -expected_utility;

            double regret = utility_for_player - expected_for_player;
            regret_sum[info_set_key][action] += opponent_reach * regret;
        }

        double my_reach = (current_player == 1) ? reach_p1 : reach_p2;
        double new_weight = get_strategy_weight(my_reach);
        double old_weight = total_weight[info_set_key];
        total_weight[info_set_key] = old_weight + new_weight;

        if (total_weight[info_set_key] > 1e-10) {
            for (const auto& action : actions) {
                avg_strategy[info_set_key][action] =
                    (avg_strategy[info_set_key][action] * old_weight +
                     strategy[action] * new_weight) / total_weight[info_set_key];
            }
        }
    }

    return expected_utility;
}

template<typename Rules>
double CFRTrainer<Rules>::cfr_external_sampling(GameState<Rules>& state, int traversing_player,
                                                double reach_p1, double reach_p2, double sample_prob) {

    if (state.is_terminal()) {
        return state.get_utility(1);
    }

    int current_player = state.current_player;
    uint64_t info_set_key = state.get_info_set_key(current_player);
    ActionList<Rules> actions = state.get_legal_actions();

    auto strategy = get_strategy(info_set_key, actions);

    // Traversing player: explore all actions
    if (current_player == traversing_player) {
        ActionMap action_utilities;
        double expected_utility = 0.0;

        for (const auto& action : actions) {
            GameState<Rules> next_state = apply_action(state, action);

            double utility;
            if (current_player == 1) {
                utility = cfr_external_sampling(next_state, traversing_player,
                                               reach_p1 * strategy[action], reach_p2, sample_prob);
            } else {
                utility = cfr_external_sampling(next_state, traversing_player,
                                               reach_p1, reach_p2 * strategy[action], sample_prob);
            }

            action_utilities[action] = utility;
            expected_utility += strategy[action] * utility;
        }

        double opponent_reach = (current_player == 1) ? reach_p2 : reach_p1;

        for (const auto& action : actions) {
            double utility_for_player = (current_player == 1) ?
                action_utilities[action] : -action_utilities[action];
            double expected_for_player = (current_player == 1) ?
                expected_utility : -expected_utility;

            double regret = utility_for_player - expected_for_player;
            regret_sum[info_set_key][action] += (opponent_reach / sample_prob) * regret;
        }

        double my_reach = (current_player == 1) ? reach_p1 : reach_p2;
        double new_weight = get_strategy_weight(my_reach);
        double old_weight = total_weight[info_set_key];
        total_weight[info_set_key] = old_weight + new_weight;

        if (total_weight[info_set_key] > 1e-10) {
            for (const auto& action : actions) {
                avg_strategy[info_set_key][action] =
                    (avg_strategy[info_set_key][action] * old_weight +
                     strategy[action] * new_weight) / total_weight[info_set_key];
            }
        }

        return expected_utility;
    }
    // Opponent: sample one action
    else {
        thread_local static std::mt19937 gen(std::random_device{}());

        std::vector<double> probs;
        probs.reserve(actions.count);
        for (const auto& action : actions) {
            probs.push_back(strategy[action]);
        }

        std::discrete_distribution<> dist(probs.begin(), probs.end());
        int sampled_idx = dist(gen);
        GameAction sampled_action = actions.actions[sampled_idx];
        double action_prob = strategy[sampled_action];

        GameState<Rules> next_state = apply_action(state, sampled_action);

        double utility;
        if (current_player == 1) {
            utility = cfr_external_sampling(next_state, traversing_player,
                                           reach_p1 * action_prob, reach_p2,
                                           sample_prob * action_prob);
        } else {
            utility = cfr_external_sampling(next_state, traversing_player,
                                           reach_p1, reach_p2 * action_prob,
                                           sample_prob * action_prob);
        }

        return utility;
    }
}

template<typename Rules>
void CFRTrainer<Rules>::save_strategy(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open " << filename << "\n";
        return;
    }

    file << "{\n";
    bool first_infoset = true;

    for (const auto& [info_set_key, action_map] : avg_strategy) {
        bool has_nan = false;
        for (const auto& [action, prob] : action_map) {
            if (std::isnan(prob)) { has_nan = true; break; }
        }
        if (has_nan) continue;

        if (!first_infoset) file << ",\n";
        first_infoset = false;

        file << "  \"0x" << std::hex << info_set_key << std::dec << "\": {";

        bool first_action = true;
        for (const auto& [action, prob] : action_map) {
            if (!first_action) file << ", ";
            first_action = false;
            file << "\"" << game_action_to_string<Rules>(action) << "\": " << prob;
        }
        file << "}";
    }

    file << "\n}\n";
    std::cout << "Saved " << avg_strategy.size() << " info sets to " << filename << "\n";
}

template<typename Rules>
void CFRTrainer<Rules>::load_strategy(const std::string& filename) {
    (void)filename;
}

// ============================================================================
// Exploitability Estimation
// ============================================================================

// ============================================================================
// Two-Pass Best Response Algorithm
// ============================================================================
// Pass 1: Traverse all states, collecting expected action values for each info set
// Pass 2: Evaluate the game value using the computed optimal policy

template<typename Rules>
void CFRTrainer<Rules>::compute_br_action_values(
    GameState<Rules>& state,
    int br_player,
    double state_prob,
    std::unordered_map<uint64_t, std::unordered_map<GameAction, double,
        GameActionHash<Rules>, GameActionEqual<Rules>>>& action_values,
    std::unordered_map<uint64_t, double>& info_set_reach,
    const std::unordered_map<uint64_t, GameAction>& br_policy) {

    if (state.is_terminal() || state_prob < 1e-12) {
        return;
    }

    int current_player = state.current_player;
    uint64_t info_set_key = state.get_info_set_key(current_player);
    ActionList<Rules> actions = state.get_legal_actions();

    if (current_player == br_player) {
        // BR player's info set - collect action values
        info_set_reach[info_set_key] += state_prob;

        for (const auto& action : actions) {
            GameState<Rules> next_state = apply_action(state, action);

            // Compute value of this action from this state
            double action_value = evaluate_br_policy(next_state, br_player, br_policy);

            // Accumulate weighted action value for this info set
            action_values[info_set_key][action] += state_prob * action_value;

            // Continue collecting for deeper info sets
            compute_br_action_values(next_state, br_player, state_prob,
                                     action_values, info_set_reach, br_policy);
        }
    } else {
        // Opponent follows average strategy
        auto strategy = get_average_strategy(info_set_key, actions);

        for (const auto& action : actions) {
            double action_prob = strategy[action];
            if (action_prob > 1e-12) {
                GameState<Rules> next_state = apply_action(state, action);
                compute_br_action_values(next_state, br_player, state_prob * action_prob,
                                         action_values, info_set_reach, br_policy);
            }
        }
    }
}

template<typename Rules>
double CFRTrainer<Rules>::evaluate_br_policy(
    GameState<Rules>& state,
    int br_player,
    const std::unordered_map<uint64_t, GameAction>& br_policy) {

    if (state.is_terminal()) {
        return state.get_utility(1);
    }

    int current_player = state.current_player;
    uint64_t info_set_key = state.get_info_set_key(current_player);
    ActionList<Rules> actions = state.get_legal_actions();

    if (current_player == br_player) {
        // BR player follows the computed policy
        auto policy_it = br_policy.find(info_set_key);
        if (policy_it != br_policy.end()) {
            GameState<Rules> next_state = apply_action(state, policy_it->second);
            return evaluate_br_policy(next_state, br_player, br_policy);
        } else {
            // Info set not in policy yet - use first action (shouldn't happen in pass 2)
            GameState<Rules> next_state = apply_action(state, actions.actions[0]);
            return evaluate_br_policy(next_state, br_player, br_policy);
        }
    } else {
        // Opponent follows average strategy
        auto strategy = get_average_strategy(info_set_key, actions);
        double expected_value = 0.0;

        for (const auto& action : actions) {
            GameState<Rules> next_state = apply_action(state, action);
            double value = evaluate_br_policy(next_state, br_player, br_policy);
            expected_value += strategy[action] * value;
        }

        return expected_value;
    }
}

template<typename Rules>
double CFRTrainer<Rules>::estimate_exploitability_quiet(int num_samples) {
    // Compute exact exploitability using iterative best response
    // num_samples parameter is ignored - we use exact computation
    (void)num_samples;

    // Compute BR for player 1
    std::unordered_map<uint64_t, GameAction> br_policy1;
    constexpr int MAX_BR_ITERATIONS = 100;

    for (int iter = 0; iter < MAX_BR_ITERATIONS; iter++) {
        std::unordered_map<uint64_t, std::unordered_map<GameAction, double,
            GameActionHash<Rules>, GameActionEqual<Rules>>> action_values;
        std::unordered_map<uint64_t, double> info_set_reach;

        // Pass 1: Collect action values across all deals
        for (const auto& deal : all_deals) {
            GameState<Rules> state = deal.state;
            compute_br_action_values(state, 1, deal.probability,
                                     action_values, info_set_reach, br_policy1);
        }

        // Update BR policy: pick best action for each info set
        bool policy_changed = false;
        for (auto& [info_set_key, av_map] : action_values) {
            double reach = info_set_reach[info_set_key];
            if (reach < 1e-12) continue;

            // Find action with highest expected value (P1 maximizes)
            GameAction best_action = av_map.begin()->first;
            double best_value = -1e9;

            for (auto& [action, total_value] : av_map) {
                double expected_value = total_value / reach;
                if (expected_value > best_value) {
                    best_value = expected_value;
                    best_action = action;
                }
            }

            auto old_it = br_policy1.find(info_set_key);
            if (old_it == br_policy1.end() || !(old_it->second == best_action)) {
                br_policy1[info_set_key] = best_action;
                policy_changed = true;
            }
        }

        if (!policy_changed) break;
    }

    // Compute BR for player 2
    std::unordered_map<uint64_t, GameAction> br_policy2;

    for (int iter = 0; iter < MAX_BR_ITERATIONS; iter++) {
        std::unordered_map<uint64_t, std::unordered_map<GameAction, double,
            GameActionHash<Rules>, GameActionEqual<Rules>>> action_values;
        std::unordered_map<uint64_t, double> info_set_reach;

        // Pass 1: Collect action values across all deals
        for (const auto& deal : all_deals) {
            GameState<Rules> state = deal.state;
            compute_br_action_values(state, 2, deal.probability,
                                     action_values, info_set_reach, br_policy2);
        }

        // Update BR policy: pick best action for each info set
        bool policy_changed = false;
        for (auto& [info_set_key, av_map] : action_values) {
            double reach = info_set_reach[info_set_key];
            if (reach < 1e-12) continue;

            // Find action with lowest expected value (P2 minimizes P1's utility)
            GameAction best_action = av_map.begin()->first;
            double best_value = 1e9;

            for (auto& [action, total_value] : av_map) {
                double expected_value = total_value / reach;
                if (expected_value < best_value) {
                    best_value = expected_value;
                    best_action = action;
                }
            }

            auto old_it = br_policy2.find(info_set_key);
            if (old_it == br_policy2.end() || !(old_it->second == best_action)) {
                br_policy2[info_set_key] = best_action;
                policy_changed = true;
            }
        }

        if (!policy_changed) break;
    }

    // Pass 2: Evaluate the game value with optimal policies
    double total_br1 = 0.0;
    double total_br2 = 0.0;

    for (const auto& deal : all_deals) {
        GameState<Rules> state1 = deal.state;
        double br1 = evaluate_br_policy(state1, 1, br_policy1);

        GameState<Rules> state2 = deal.state;
        double br2 = evaluate_br_policy(state2, 2, br_policy2);

        total_br1 += deal.probability * br1;
        total_br2 += deal.probability * br2;
    }

    return (total_br1 - total_br2) / 2.0;
}

template<typename Rules>
double CFRTrainer<Rules>::estimate_exploitability(int num_samples) {
    // Exploitability = (BR_p1 - BR_p2) / 2
    // where BR_p1 is P1's value when playing best response against P2's strategy
    // and BR_p2 is P1's value when P2 plays best response against P1's strategy
    //
    // At Nash equilibrium: BR_p1 = BR_p2 = game_value, so exploitability = 0
    //
    // Note: num_samples is ignored - we compute exact exploitability
    (void)num_samples;

    std::cout << "Computing exact exploitability using iterative best response...\n";
    std::cout << "Processing " << all_deals.size() << " deals\n";

    // Compute BR for player 1
    std::unordered_map<uint64_t, GameAction> br_policy1;
    constexpr int MAX_BR_ITERATIONS = 100;
    int p1_iters = 0;

    for (int iter = 0; iter < MAX_BR_ITERATIONS; iter++) {
        std::unordered_map<uint64_t, std::unordered_map<GameAction, double,
            GameActionHash<Rules>, GameActionEqual<Rules>>> action_values;
        std::unordered_map<uint64_t, double> info_set_reach;

        for (const auto& deal : all_deals) {
            GameState<Rules> state = deal.state;
            compute_br_action_values(state, 1, deal.probability,
                                     action_values, info_set_reach, br_policy1);
        }

        bool policy_changed = false;
        for (auto& [info_set_key, av_map] : action_values) {
            double reach = info_set_reach[info_set_key];
            if (reach < 1e-12) continue;

            GameAction best_action = av_map.begin()->first;
            double best_value = -1e9;

            for (auto& [action, total_value] : av_map) {
                double expected_value = total_value / reach;
                if (expected_value > best_value) {
                    best_value = expected_value;
                    best_action = action;
                }
            }

            auto old_it = br_policy1.find(info_set_key);
            if (old_it == br_policy1.end() || !(old_it->second == best_action)) {
                br_policy1[info_set_key] = best_action;
                policy_changed = true;
            }
        }

        p1_iters = iter + 1;
        if (!policy_changed) break;
    }
    std::cout << "P1 BR converged in " << p1_iters << " iterations (" << br_policy1.size() << " info sets)\n";

    // Compute BR for player 2
    std::unordered_map<uint64_t, GameAction> br_policy2;
    int p2_iters = 0;

    for (int iter = 0; iter < MAX_BR_ITERATIONS; iter++) {
        std::unordered_map<uint64_t, std::unordered_map<GameAction, double,
            GameActionHash<Rules>, GameActionEqual<Rules>>> action_values;
        std::unordered_map<uint64_t, double> info_set_reach;

        for (const auto& deal : all_deals) {
            GameState<Rules> state = deal.state;
            compute_br_action_values(state, 2, deal.probability,
                                     action_values, info_set_reach, br_policy2);
        }

        bool policy_changed = false;
        for (auto& [info_set_key, av_map] : action_values) {
            double reach = info_set_reach[info_set_key];
            if (reach < 1e-12) continue;

            GameAction best_action = av_map.begin()->first;
            double best_value = 1e9;

            for (auto& [action, total_value] : av_map) {
                double expected_value = total_value / reach;
                if (expected_value < best_value) {
                    best_value = expected_value;
                    best_action = action;
                }
            }

            auto old_it = br_policy2.find(info_set_key);
            if (old_it == br_policy2.end() || !(old_it->second == best_action)) {
                br_policy2[info_set_key] = best_action;
                policy_changed = true;
            }
        }

        p2_iters = iter + 1;
        if (!policy_changed) break;
    }
    std::cout << "P2 BR converged in " << p2_iters << " iterations (" << br_policy2.size() << " info sets)\n";

    // Evaluate game value with optimal policies
    double total_br1 = 0.0;
    double total_br2 = 0.0;

    for (const auto& deal : all_deals) {
        GameState<Rules> state1 = deal.state;
        double br1 = evaluate_br_policy(state1, 1, br_policy1);

        GameState<Rules> state2 = deal.state;
        double br2 = evaluate_br_policy(state2, 2, br_policy2);

        total_br1 += deal.probability * br1;
        total_br2 += deal.probability * br2;
    }

    double exploitability = (total_br1 - total_br2) / 2.0;

    std::cout << "BR value (P1 optimal): " << std::fixed << std::setprecision(4) << total_br1
              << "\nBR value (P2 optimal): " << total_br2
              << "\nExploitability: " << exploitability << "\n";

    return exploitability;
}

template<typename Rules>
void CFRTrainer<Rules>::save_convergence_data(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open " << filename << "\n";
        return;
    }

    file << "iteration,exploitability,info_sets\n";
    for (const auto& [iter, exploit, info_sets] : convergence_data) {
        file << iter << "," << std::fixed << std::setprecision(6) << exploit << "," << info_sets << "\n";
    }

    std::cout << "Saved convergence data (" << convergence_data.size() << " points) to " << filename << "\n";
}
