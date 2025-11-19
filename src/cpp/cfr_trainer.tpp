// Template implementations for CFRTrainer
// This file is included by cfr_trainer.h - DO NOT include directly

#include <algorithm>
#include <numeric>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <iomanip>
#include <chrono>
#include <omp.h>

// Enhanced progress bar with time estimation
static void print_progress_bar_with_time(int current, int total,
                                         std::chrono::steady_clock::time_point start_time,
                                         int bar_width = 40) {
    float progress = (float)current / total;
    int pos = bar_width * progress;

    // Calculate elapsed time
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();

    // Calculate time estimates
    double time_per_iter = (current > 0) ? (double)elapsed / current : 0.0;
    double eta_seconds = time_per_iter * (total - current);
    double iters_per_sec = (elapsed > 0) ? (double)current / elapsed : 0.0;

    // Format time strings
    auto format_time = [](double seconds) -> std::string {
        int hours = (int)(seconds / 3600);
        int mins = (int)((seconds - hours * 3600) / 60);
        int secs = (int)(seconds - hours * 3600 - mins * 60);

        if (hours > 0) {
            return std::to_string(hours) + "h " + std::to_string(mins) + "m " + std::to_string(secs) + "s";
        } else if (mins > 0) {
            return std::to_string(mins) + "m " + std::to_string(secs) + "s";
        } else {
            return std::to_string(secs) + "s";
        }
    };

    // Print progress bar
    std::cout << "[";
    for (int i = 0; i < bar_width; ++i) {
        if (i < pos) std::cout << "=";
        else if (i == pos) std::cout << ">";
        else std::cout << " ";
    }
    std::cout << "] " << std::setw(3) << int(progress * 100.0) << "% "
              << current << "/" << total << " | ";

    // Print time information
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Elapsed: " << format_time(elapsed) << " | ";
    std::cout << "ETA: " << format_time(eta_seconds) << " | ";
    std::cout << std::setprecision(1) << iters_per_sec << " it/s";
    std::cout << "\r";
    std::cout.flush();

    if (current == total) {
        std::cout << "\nTotal time: " << format_time(elapsed) << "\n";
    }
}

// ============================================================================
// CFRTrainer Implementation
// ============================================================================

template<typename Rules>
CFRTrainer<Rules>::CFRTrainer() : current_iteration(0), num_rollouts(10),
                            max_depth(DEPTH_LIMIT), debug_enabled(false),
                            debug_iteration(-1), max_debug_states(10),
                            debug_state_counter(0) {
    // Debug: Print optimization settings
    std::cout << "\n=== CFR Configuration ===\n";
    std::cout << "Variant: " << Rules::get_variant_name() << "\n";
    std::cout << "Max influences per player: " << Rules::MAX_INFLUENCES_PER_PLAYER << "\n";
    std::cout << "Num influence types: " << Rules::NUM_INFLUENCE_TYPES << "\n";
    std::cout << "Depth limit: " << DEPTH_LIMIT << "\n";
    std::cout << "Abstraction mode: ";
    if constexpr (ABSTRACTION_MODE == AbstractionMode::NONE) {
        std::cout << "NONE (exact coins)\n";
    } else if constexpr (ABSTRACTION_MODE == AbstractionMode::ASYMMETRIC) {
        std::cout << "ASYMMETRIC (exact my coins, abstract opponent)\n";
    } else if constexpr (ABSTRACTION_MODE == AbstractionMode::SYMMETRIC) {
        std::cout << "SYMMETRIC (abstract both players)\n";
    } else {
        std::cout << "FINE_GRAINED (5 buckets)\n";
    }
    std::cout << "Early termination: " << (ENABLE_EARLY_TERMINATION ? "ENABLED" : "DISABLED") << "\n";
    if constexpr (ENABLE_EARLY_TERMINATION) {
        std::cout << "  Min depth: " << EARLY_TERM_MIN_DEPTH << "\n";
        std::cout << "  Score threshold: " << EARLY_TERM_SCORE_THRESHOLD << "\n";
    }
    std::cout << "=========================\n\n";
}

template<typename Rules>
typename CFRTrainer<Rules>::ActionMap CFRTrainer<Rules>::get_strategy(
    uint64_t info_set_key,
    const ActionList<Rules>& actions) {

    ActionMap strategy;
    int num_actions = actions.count;

    // Read-only lookup
    auto it = regret_sum.find(info_set_key);

    if (it != regret_sum.end()) {
        const auto& regrets = it->second;

        // for (const auto& action : actions) {
        //     auto regret_it = regrets.find(action);
        //     double regret = (regret_it != regrets.end()) ? regret_it->second : 0.0;
        //     strategy[action] = std::max(0.0, regret);
        // }

        strategy = regrets;

        double sum = 0.0;
        for (const auto &[_, prob] : strategy) {
            sum += prob;
        }

        // positive regrets
        if (sum > 0.0) {
            for (auto &[_, prob] : strategy) {
                prob /= sum;
            }
        }
        // no positive regrets
        else {
            for (auto &[_, prob] : strategy) {
                prob = 1.0 / num_actions;
            }
        }
    } else {
        // Info set never visited - return uniform strategy
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

    auto& stored_avg = avg_strategy[info_set_key];

    if (!stored_avg.empty()) {
        return stored_avg;
    }

    // If never visited, return uniform distribution
    ActionMap uniform;
    double uniform_prob = 1.0 / actions.count;
    for (const auto& action : actions) {
        uniform[action] = uniform_prob;
    }
    return uniform;
}

template<typename Rules>
void CFRTrainer<Rules>::train(int iterations) {
    std::cout << "Training CFR+ for " << iterations << " iterations...\n";
    std::cout << "OpenMP threads available: " << omp_get_max_threads() << "\n";
    std::cout << "Depth 0-" << max_depth << ": Vanilla CFR (full exploration)\n";
    std::cout << "Depth " << (max_depth+1) << "+: External Sampling MCCFR (sample opponent actions)\n";

    if (debug_enabled) {
        if (debug_iteration < 0 || debug_iteration >= iterations) {
            std::cout << "\nDEBUG MODE: Enabled for ALL iterations\n";
            std::cout << "WARNING: This will produce a LOT of output!\n\n";
        } else {
            std::cout << "\nDEBUG MODE: Enabled for iteration " << debug_iteration << "\n";
            std::cout << "Max debug states per iteration: " << max_debug_states << "\n\n";
        }
    } else {
        std::cout << "\n";
    }

    auto start_time = std::chrono::steady_clock::now();

    for (int i = 0; i < iterations; i++) {
        current_iteration++;

        // Reset debug counter at the start of each iteration
        if (debug_enabled && current_iteration == debug_iteration) {
            debug_state_counter = 0;
            std::cout << "\n" << std::string(70, '=') << "\n";
            std::cout << "DEBUG: Starting iteration " << current_iteration << "\n";
            std::cout << std::string(70, '=') << "\n";
        }

        // Traverse game tree from player 1's perspective
        if (debug_enabled && current_iteration == debug_iteration) {
            std::cout << "\n--- Traversing from Player 1's perspective ---\n";
        }
        GameState<Rules> state1 = create_initial_state<Rules>();
        double util1 = cfr(state1, 1, 1.0, 1.0);

        // Traverse game tree from player 2's perspective
        if (debug_enabled && current_iteration == debug_iteration) {
            std::cout << "\n--- Traversing from Player 2's perspective ---\n";
            debug_state_counter = 0;  // Reset for second traversal
        }
        GameState<Rules> state2 = create_initial_state<Rules>();
        double util2 = cfr(state2, 2, 1.0, 1.0);

        // Force synchronization
        (void)util1;
        (void)util2;

        // Print debug statistics for this iteration
        if (debug_enabled && current_iteration == debug_iteration) {
            std::cout << "\n" << std::string(70, '=') << "\n";
            std::cout << "DEBUG: Iteration " << current_iteration << " complete\n";
            std::cout << "Player 1 utility: " << std::fixed << std::setprecision(6) << util1 << "\n";
            std::cout << "Player 2 utility: " << std::fixed << std::setprecision(6) << util2 << "\n";
            std::cout << std::string(70, '=') << "\n\n";
            print_debug_statistics();
        }

        // Update progress bar (skip if debugging current iteration)
        if (!(debug_enabled && current_iteration == debug_iteration)) {
            if (iterations <= 10000 || current_iteration % 10 == 0 || current_iteration == iterations) {
                print_progress_bar_with_time(current_iteration, iterations, start_time);
            }
        }
    }

    std::cout << "Training complete! Final info set count: " << get_info_set_count() << "\n";
}

template<typename Rules>
double CFRTrainer<Rules>::cfr(GameState<Rules>& state, int traversing_player,
                              double reach_p1, double reach_p2) {
    
    if (state.is_terminal()) {
        // Always return utility from player 1's perspective
        return state.get_utility(1);
    }
    
    if (state.depth >= max_depth) {
        return cfr_external_sampling(state, traversing_player, reach_p1, reach_p2, 1.0);
    }
    
    int current_player = state.current_player;
    uint64_t info_set_key = state.get_info_set_key(current_player);
    ActionList<Rules> actions = state.get_legal_actions();
    
    auto strategy = get_strategy(info_set_key, actions);
    
    // Compute utilities for each action
    ActionMap action_utilities;
    double expected_utility = 0.0;
    
    for (const auto& action : actions) {
        GameState<Rules> next_state = apply_action(state, action);
        
        double utility;
        if (current_player == 1) {
            utility = cfr(next_state, traversing_player,
                         reach_p1 * strategy[action], reach_p2);
        } else {
            utility = cfr(next_state, traversing_player,
                         reach_p1, reach_p2 * strategy[action]);
        }
        
        action_utilities[action] = utility;
        expected_utility += strategy[action] * utility;
    }
    
    // Update regrets ONLY for traversing player
    if (current_player == traversing_player) {
        double opponent_reach = (current_player == 1) ? reach_p2 : reach_p1;
        
        for (const auto& action : actions) {
            // IMPORTANT: Compute regret from player's perspective
            double utility_for_player = (current_player == 1) ? 
                action_utilities[action] : -action_utilities[action];
            double expected_for_player = (current_player == 1) ? 
                expected_utility : -expected_utility;
            
            double regret = utility_for_player - expected_for_player;
            double old_regret = regret_sum[info_set_key][action];
            double new_regret = old_regret + opponent_reach * regret;
            
            // CFR+ update
            regret_sum[info_set_key][action] = std::max(0.0, new_regret);
        }
        
        // Update average strategy ONLY for traversing player
        double my_reach = (current_player == 1) ? reach_p1 : reach_p2;
        double new_weight = current_iteration * current_iteration * my_reach;
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
    
    // Always return utility from player 1's perspective
    return expected_utility;
}

template<typename Rules>
double CFRTrainer<Rules>::cfr_external_sampling(GameState<Rules>& state, int traversing_player,
                                                double reach_p1, double reach_p2, double sample_prob) {
    
    if (state.is_terminal()) {
        // Always return utility from player 1's perspective
        return state.get_utility(1);
    }
    
    int current_player = state.current_player;
    uint64_t info_set_key = state.get_info_set_key(current_player);
    ActionList<Rules> actions = state.get_legal_actions();
    
    auto strategy = get_strategy(info_set_key, actions);
    
    // TRAVERSING PLAYER: Explore all actions (no sampling)
    if (current_player == traversing_player) {
        ActionMap action_utilities;
        double expected_utility = 0.0;
        
        // Explore all actions for the traversing player
        for (const auto& action : actions) {
            GameState<Rules> next_state = apply_action(state, action);
            
            double utility;
            if (current_player == 1) {
                utility = cfr_external_sampling(next_state, traversing_player,
                                              reach_p1 * strategy[action], reach_p2,
                                              sample_prob);  // Don't update sample_prob for traverser
            } else {
                utility = cfr_external_sampling(next_state, traversing_player,
                                              reach_p1, reach_p2 * strategy[action],
                                              sample_prob);  // Don't update sample_prob for traverser
            }
            
            action_utilities[action] = utility;
            expected_utility += strategy[action] * utility;
        }
        
        // Update regrets with importance sampling correction
        double opponent_reach = (current_player == 1) ? reach_p2 : reach_p1;
        
        for (const auto& action : actions) {
            double utility_for_player = (current_player == 1) ? 
                action_utilities[action] : -action_utilities[action];
            double expected_for_player = (current_player == 1) ? 
                expected_utility : -expected_utility;
            
            double regret = utility_for_player - expected_for_player;
            
            // CFR+ update with importance sampling correction
            double old_regret = regret_sum[info_set_key][action];
            double new_regret = old_regret + (opponent_reach / sample_prob) * regret;
            
            // Apply CFR+ (max with 0)
            regret_sum[info_set_key][action] = std::max(0.0, new_regret);
        }
        
        // Update average strategy for traversing player
        // Note: In MCCFR, we still need to track average strategy for convergence
        double my_reach = (current_player == 1) ? reach_p1 : reach_p2;
        
        // Use linear weighting (iteration^2) scaled by reach probability
        // No importance sampling correction needed for average strategy in external sampling
        double new_weight = current_iteration * current_iteration * my_reach;
        double old_weight = total_weight[info_set_key];
        total_weight[info_set_key] = old_weight + new_weight;
        
        if (total_weight[info_set_key] > 1e-10) {
            for (const auto& action : actions) {
                avg_strategy[info_set_key][action] =
                    (avg_strategy[info_set_key][action] * old_weight + 
                     strategy[action] * new_weight) / total_weight[info_set_key];
            }
        }
        
        // Return expected utility from player 1's perspective
        return expected_utility;
    }
    // OPPONENT: Sample one action
    else {
        thread_local static std::mt19937 gen(std::random_device{}());
        
        // Sample action according to strategy
        std::vector<double> probs;
        probs.reserve(actions.count);
        for (const auto& action : actions) {
            probs.push_back(strategy[action]);
        }
        
        std::discrete_distribution<> dist(probs.begin(), probs.end());
        int sampled_idx = dist(gen);
        GameAction sampled_action = actions.actions[sampled_idx];
        double action_prob = strategy[sampled_action];
        
        // Apply sampled action and recurse
        GameState<Rules> next_state = apply_action(state, sampled_action);
        
        double utility;
        if (current_player == 1) {
            utility = cfr_external_sampling(next_state, traversing_player,
                                          reach_p1 * action_prob, reach_p2,
                                          sample_prob * action_prob);  // Update sample_prob for opponent
        } else {
            utility = cfr_external_sampling(next_state, traversing_player,
                                          reach_p1, reach_p2 * action_prob,
                                          sample_prob * action_prob);  // Update sample_prob for opponent
        }
        
        return utility;
    }
}

template<typename Rules>
void CFRTrainer<Rules>::save_strategy(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file for writing: " << filename << "\n";
        return;
    }

    file << "{\n";

    bool first_infoset = true;
    int nan_count = 0;
    int skipped_infosets = 0;

    for (const auto& [info_set_key, action_map] : avg_strategy) {
        // Check for NaN values
        bool has_nan = false;
        for (const auto& [action, prob] : action_map) {
            if (std::isnan(prob)) {
                has_nan = true;
                nan_count++;
            }
        }

        if (has_nan) {
            skipped_infosets++;
            continue;
        }

        if (!first_infoset) {
            file << ",\n";
        }
        first_infoset = false;

        file << "  \"0x" << std::hex << info_set_key << std::dec << "\": {\n";

        bool first_action = true;
        for (const auto& [action, prob] : action_map) {
            if (!first_action) {
                file << ",\n";
            }
            first_action = false;

            file << "    \"" << game_action_to_string<Rules>(action) << "\": " << prob;
        }

        file << "\n  }";
    }

    file << "\n}\n";
    file.close();

    std::cout << "Saved " << (avg_strategy.size() - skipped_infosets) << " information sets to " << filename << "\n";
    if (nan_count > 0) {
        std::cerr << "Warning: Skipped " << skipped_infosets << " info sets with " << nan_count << " NaN values\n";
    }
}

template<typename Rules>
void CFRTrainer<Rules>::load_strategy(const std::string& filename) {
    std::cerr << "Warning: load_strategy() not yet implemented\n";
    (void)filename;
}

// ============================================================================
// Debug Functions
// ============================================================================

template<typename Rules>
void CFRTrainer<Rules>::reset_debug_counters() {
    state_visit_count.clear();
    action_count.clear();
    debug_state_counter = 0;
}

template<typename Rules>
void CFRTrainer<Rules>::print_debug_statistics() const {
    std::cout << "\n=== CFR Debug Statistics ===\n";
    std::cout << "Total unique states visited: " << state_visit_count.size() << "\n";

    // Top 10 most visited states
    std::vector<std::pair<uint64_t, int>> visit_pairs(state_visit_count.begin(), state_visit_count.end());
    std::sort(visit_pairs.begin(), visit_pairs.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    std::cout << "\nTop 10 most visited states:\n";
    for (size_t i = 0; i < std::min(size_t(10), visit_pairs.size()); i++) {
        std::cout << "  State 0x" << std::hex << visit_pairs[i].first << std::dec
                  << ": " << visit_pairs[i].second << " visits\n";
    }

    // Action distribution
    std::cout << "\nAction distribution:\n";
    std::vector<std::pair<std::string, int>> action_pairs(action_count.begin(), action_count.end());
    std::sort(action_pairs.begin(), action_pairs.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    int total_actions = 0;
    for (const auto& [_, count] : action_count) {
        total_actions += count;
    }

    for (const auto& [action, count] : action_pairs) {
        double pct = (total_actions > 0) ? (100.0 * count / total_actions) : 0.0;
        std::cout << "  " << std::setw(15) << action << ": "
                  << std::setw(8) << count << " ("
                  << std::fixed << std::setprecision(2) << std::setw(6) << pct << "%)\n";
    }

    std::cout << "  " << std::string(15, '-') << "  " << std::string(8, '-') << "\n";
    std::cout << "  " << std::setw(15) << "TOTAL" << ": " << std::setw(8) << total_actions << "\n";

    std::cout << "============================\n\n";
}

// Helper function to print state details (for debugging)
template<typename Rules>
std::string debug_state_string(const GameState<Rules>& state) {
    std::ostringstream oss;
    oss << "P1: " << (int)state.p1_influence_count << " inf, "
        << (int)state.p1_coins << " coins | ";
    oss << "P2: " << (int)state.p2_influence_count << " inf, "
        << (int)state.p2_coins << " coins | ";
    oss << "Depth: " << state.depth << " | ";
    oss << "Turn: P" << (int)state.current_player;

    if (state.has_pending_action) {
        oss << " | Pending: P" << state.pending_action_player
            << " " << Rules::action_to_string(state.pending_action_type);
    }

    return oss.str();
}
