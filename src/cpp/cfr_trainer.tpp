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

    // Print all deals with their probabilities
    std::cout << "\n=== Deal Enumeration ===\n";
    for (size_t i = 0; i < all_deals.size(); i++) {
        const auto& deal = all_deals[i];
        std::cout << "Deal " << (i + 1) << "/" << all_deals.size()
                  << " (prob=" << std::fixed << std::setprecision(6) << deal.probability << "): ";

        // P1 influences
        std::cout << "P1[";
        for (uint8_t j = 0; j < deal.state.p1_influence_count; j++) {
            if (j > 0) std::cout << ", ";
            std::cout << Rules::influence_to_string(deal.state.p1_influences[j]);
        }
        std::cout << "] vs P2[";

        // P2 influences
        for (uint8_t j = 0; j < deal.state.p2_influence_count; j++) {
            if (j > 0) std::cout << ", ";
            std::cout << Rules::influence_to_string(deal.state.p2_influences[j]);
        }
        std::cout << "]\n";
    }
    std::cout << "========================\n\n";
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
    } 
    else {
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
        // Verify the stored strategy contains all legal actions
        bool has_all_actions = true;
        for (const auto& action : actions) {
            if (it->second.find(action) == it->second.end()) {
                has_all_actions = false;
                break;
            }
        }

        if (has_all_actions) {
            // Return stored strategy, but normalize it to be safe
            ActionMap normalized;
            double sum = 0.0;
            for (const auto& action : actions) {
                double prob = it->second.at(action);
                normalized[action] = prob;
                sum += prob;
            }

            // Normalize if sum is not 1 (shouldn't happen, but safety check)
            if (sum > 1e-10) {
                for (auto& [action, prob] : normalized) {
                    prob /= sum;
                }
            } else {
                // If sum is too small, fall through to uniform
                has_all_actions = false;
            }

            if (has_all_actions) {
                return normalized;
            }
        }
    }

    // Fallback to uniform strategy
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

    // Discount REGRETS only (positive and negative separately)
    for (auto& [info_set_key, action_map] : regret_sum) {
        for (auto& [action, regret] : action_map) {
            regret *= (regret > 0) ? pos_discount : neg_discount;
        }
    }

    // NOTE: Do NOT discount total_weight for average strategy!
    // The t^γ weighting is already applied in get_strategy_weight().
    // Discounting here would double-apply the weighting and break convergence.
}

template<typename Rules>
void CFRTrainer<Rules>::train(int iterations, int exploitability_interval) {
    std::cout << "Training DCFR for " << iterations << " iterations "
              << "(alpha=" << dcfr_alpha << ", beta=" << dcfr_beta << ", gamma=" << dcfr_gamma << ")\n";
    std::cout << "Enumerating over " << all_deals.size() << " possible deals per iteration\n";

    auto start_time = std::chrono::steady_clock::now();

    for (int i = 0; i < iterations; i++) {
        current_iteration++;

        // Iterate over ALL possible initial deals, weighted by probability
        size_t deal_idx = 0;
        for (const auto& deal : all_deals) {
            deal_idx++;

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

        // Compute exploitability at specified intervals
        if (exploitability_interval > 0 && current_iteration % exploitability_interval == 0) {
            double expl = compute_exploitability();
            
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count() / 1000.0;
            
            std::cout << "\n[Iter " << current_iteration << "] "
                      << "Exploitability: " << std::fixed << std::setprecision(6) << expl
                      << " | Info sets: " << get_info_set_count()
                      << " | Time: " << format_time(elapsed) << "\n";
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
double CFRTrainer<Rules>::compute_exploitability() {
    // Compute best response value for each player
    double br1_value = compute_best_response_value(1);  // P1's BR vs P2's strategy
    double br2_value = compute_best_response_value(2);  // P2's BR vs P1's strategy
    
    // In zero-sum game:
    // - At Nash: br1_value = v*, br2_value = -v*, so (br1 + br2)/2 = 0
    // - If exploitable: players can do better, so (br1 + br2)/2 > 0
    double exploitability = (br1_value + br2_value) / 2.0;
    
    // Track convergence
    exploitability_history.push_back({current_iteration, exploitability});
    
    return exploitability;
}

// ============================================================================
// Best Response Value Computation (Information-Set Consistent)
// ============================================================================

template<typename Rules>
double CFRTrainer<Rules>::compute_best_response_value(int br_player) {
    // Data structure to accumulate counterfactual action values per info set
    // Using nested struct to avoid forward declaration issues
    struct BRData {
        ActionMap action_cf_values;
        double total_cf_reach = 0.0;
        int node_count = 0;
    };
    
    using InfoSetMap = std::unordered_map<uint64_t, BRData>;
    
    // ========================================================================
    // PHASE 1: Collect counterfactual action values at each info set
    // ========================================================================
    InfoSetMap info_set_data;
    
    // Lambda for recursive traversal
    std::function<double(GameState<Rules>&, double)> collect_values;
    
    collect_values = [&](GameState<Rules>& state, double opponent_reach) -> double {
        // Terminal state
        if (state.is_terminal()) {
            return opponent_reach * state.get_utility(br_player);
        }
        
        // Depth limit
        if (state.depth >= max_depth) {
            return opponent_reach * state.get_utility(br_player);
        }
        
        int current_player = state.current_player;
        ActionList<Rules> actions = state.get_legal_actions();
        
        if (current_player == br_player) {
            // BR PLAYER'S NODE
            // Collect counterfactual value of each action at this info set
            uint64_t info_set_key = state.get_info_set_key(current_player);
            auto& data = info_set_data[info_set_key];
            data.total_cf_reach += opponent_reach;
            data.node_count++;
            
            double best_continuation = -std::numeric_limits<double>::infinity();
            
            for (const auto& action : actions) {
                GameState<Rules> next_state = apply_action(state, action);
                
                // Recurse: opponent_reach unchanged since this is BR's action
                double continuation = collect_values(next_state, opponent_reach);
                
                // Accumulate this action's counterfactual value
                data.action_cf_values[action] += continuation;
                
                best_continuation = std::max(best_continuation, continuation);
            }
            
            return best_continuation;
        }
        else {
            // OPPONENT'S NODE (Fixed Strategy Player)
            // Follow opponent's average strategy, multiply into opponent_reach
            uint64_t info_set_key = state.get_info_set_key(current_player);
            auto strategy = get_average_strategy(info_set_key, actions);
            
            double total_continuation = 0.0;
            
            for (const auto& action : actions) {
                double action_prob = strategy[action];
                
                // Skip zero-probability actions
                if (action_prob < 1e-10) continue;
                
                GameState<Rules> next_state = apply_action(state, action);
                
                // Recurse: multiply opponent_reach by this action's probability
                double continuation = collect_values(next_state, opponent_reach * action_prob);
                
                total_continuation += continuation;
            }
            
            return total_continuation;
        }
    };
    
    // Run Phase 1 over all deals
    for (const auto& deal : all_deals) {
        GameState<Rules> state = deal.state;
        collect_values(state, deal.probability);
    }
    
    // ========================================================================
    // PHASE 2: Determine best action at each information set
    // ========================================================================
    std::unordered_map<uint64_t, GameAction> best_actions;
    
    for (const auto& [info_set_key, data] : info_set_data) {
        GameAction best_action;
        double best_cf_value = -std::numeric_limits<double>::infinity();
        
        for (const auto& [action, cf_value] : data.action_cf_values) {
            if (cf_value > best_cf_value) {
                best_cf_value = cf_value;
                best_action = action;
            }
        }
        
        best_actions[info_set_key] = best_action;
    }
    
    // ========================================================================
    // PHASE 3: Compute BR value using consistent action choices
    // ========================================================================
    
    // Lambda for evaluation traversal
    std::function<double(GameState<Rules>&)> evaluate;
    
    evaluate = [&](GameState<Rules>& state) -> double {
        if (state.is_terminal()) {
            return state.get_utility(br_player);
        }
        
        if (state.depth >= max_depth) {
            return state.get_utility(br_player);
        }
        
        int current_player = state.current_player;
        ActionList<Rules> actions = state.get_legal_actions();
        
        if (current_player == br_player) {
            // BR PLAYER: Use the pre-computed best action for this info set
            uint64_t info_set_key = state.get_info_set_key(current_player);
            
            auto it = best_actions.find(info_set_key);
            if (it != best_actions.end()) {
                GameState<Rules> next_state = apply_action(state, it->second);
                return evaluate(next_state);
            }
            else {
                // Info set not seen in Phase 1 (shouldn't happen)
                double best_value = -std::numeric_limits<double>::infinity();
                for (const auto& action : actions) {
                    GameState<Rules> next_state = apply_action(state, action);
                    double value = evaluate(next_state);
                    best_value = std::max(best_value, value);
                }
                return best_value;
            }
        }
        else {
            // OPPONENT: Follow their average strategy
            uint64_t info_set_key = state.get_info_set_key(current_player);
            auto strategy = get_average_strategy(info_set_key, actions);
            
            double expected_value = 0.0;
            
            for (const auto& action : actions) {
                double action_prob = strategy[action];
                if (action_prob < 1e-10) continue;
                
                GameState<Rules> next_state = apply_action(state, action);
                double value = evaluate(next_state);
                expected_value += action_prob * value;
            }
            
            return expected_value;
        }
    };
    
    // Run Phase 3 over all deals
    double total_br_value = 0.0;
    
    for (const auto& deal : all_deals) {
        GameState<Rules> state = deal.state;
        double value = evaluate(state);
        total_br_value += deal.probability * value;
    }
    
    return total_br_value;
}

template<typename Rules>
void CFRTrainer<Rules>::save_convergence_data(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open " << filename << "\n";
        return;
    }
    
    file << "iteration,exploitability\n";
    for (const auto& [iter, expl] : exploitability_history) {
        file << iter << "," << std::scientific << std::setprecision(10) << expl << "\n";
    }
    
    std::cout << "Saved " << exploitability_history.size() << " data points to " << filename << "\n";
}
