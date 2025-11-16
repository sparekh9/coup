#include "cfr_trainer.h"
#include <algorithm>
#include <numeric>
#include <iostream>
#include <fstream>
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

CFRTrainer::CFRTrainer() : current_iteration(0), num_rollouts(10),
                            max_depth(DEPTH_LIMIT) {
    // Default to 10 rollouts for depth-limited evaluation
    // Default to DEPTH_LIMIT for maximum depth

    // Debug: Print optimization settings
    std::cout << "\n=== CFR Configuration ===\n";
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

std::unordered_map<GameAction, double, GameActionHash> CFRTrainer::get_strategy(
    uint64_t info_set_key,
    const ActionList& actions) {

    // Implement Regret Matching+ with read-only lookup
    // CRITICAL: Use find() instead of operator[] to avoid creating new entries
    // during rollouts, which would pollute the regret_sum map

    std::unordered_map<GameAction, double, GameActionHash> strategy;
    int num_actions = actions.count;

    // Read-only lookup - check if info set exists
    auto it = regret_sum.find(info_set_key);

    if (it != regret_sum.end()) {
        // Info set exists - use stored regrets
        const auto& regrets = it->second;

        for (const auto& action : actions) {
            auto regret_it = regrets.find(action);
            double regret = (regret_it != regrets.end()) ? regret_it->second : 0.0;
            strategy[action] = std::max(0.0, regret);
        }

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
        // This is common during rollouts beyond max_depth
        for (const auto& action : actions) {
            strategy[action] = 1.0 / num_actions;
        }
    }

    return strategy;
}

std::unordered_map<GameAction, double, GameActionHash> CFRTrainer::get_average_strategy(
    uint64_t info_set_key,
    const ActionList& actions) {

    // Return the stored running weighted average
    auto& stored_avg = avg_strategy[info_set_key];

    // If this info set has been visited, return the stored average
    if (!stored_avg.empty()) {
        return stored_avg;
    }

    // If never visited, return uniform distribution
    std::unordered_map<GameAction, double, GameActionHash> uniform;
    double uniform_prob = 1.0 / actions.count;
    for (const auto& action : actions) {
        uniform[action] = uniform_prob;
    }
    return uniform;
}

void CFRTrainer::train(int iterations) {
    std::cout << "Training CFR+ for " << iterations << " iterations...\n";
    std::cout << "OpenMP threads available: " << omp_get_max_threads() << "\n";
    std::cout << "Depth 0-" << max_depth << ": Vanilla CFR (full exploration)\n";
    std::cout << "Depth " << (max_depth+1) << "+: External Sampling MCCFR (sample opponent actions)\n\n";

    // Start timer
    auto start_time = std::chrono::steady_clock::now();

    for (int i = 0; i < iterations; i++) {
        current_iteration++;

        // Traverse game tree from player 1's perspective
        GameState state1 = create_initial_state();
        double util1 = cfr(state1, 1, 1.0, 1.0);

        // Traverse game tree from player 2's perspective
        GameState state2 = create_initial_state();
        double util2 = cfr(state2, 2, 1.0, 1.0);

        // Force synchronization point - ensure all work completes before next iteration
        // The cfr() calls should already block, but this makes it explicit
        (void)util1;
        (void)util2;

        // Update progress bar with time estimation (update every iteration, or every 10 for very large runs)
        if (iterations <= 10000 || current_iteration % 10 == 0 || current_iteration == iterations) {
            print_progress_bar_with_time(current_iteration, iterations, start_time);
        }
    }

    std::cout << "Training complete! Final info set count: " << get_info_set_count() << "\n";
}

double CFRTrainer::cfr(GameState& state, int traversing_player,
                       double reach_p1, double reach_p2) {

    // Base case: true terminal node (game ended)
    // if (state.p1_influence_count == 0 || state.p2_influence_count == 0) {
    if (state.is_terminal()) {
        return state.get_utility(1);  // Always return utility for player 1
    }

    // Depth limit reached: switch to External Sampling MCCFR
    // This continues learning (updating regrets) but samples opponent actions to reduce branching
    if (state.depth >= max_depth) {
        return cfr_external_sampling(state, traversing_player, reach_p1, reach_p2, 1.0);
    }

    // Get current state information
    int current_player = state.current_player;
    uint64_t info_set_key = state.get_info_set_key(current_player);
    ActionList actions = state.get_legal_actions();

    // Get current strategy for this information set
    auto strategy = get_strategy(info_set_key, actions);

    // Compute utilities for each action
    std::unordered_map<GameAction, double, GameActionHash> action_utilities;
    double expected_utility = 0.0;

    for (const auto& action : actions) {
        // Apply action to get next state
        GameState next_state = apply_action(state, action);

        // Recursively compute utility with updated reach probabilities
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

    // Update regrets (only for traversing player)
    if (current_player == traversing_player) {
        double opponent_reach = (current_player == 1) ? reach_p2 : reach_p1;

        for (const auto& action : actions) {
            double regret = action_utilities[action] - expected_utility;

            // CFR+ update: add regret and floor to 0
            regret_sum[info_set_key][action] = std::max(0.0,
                regret_sum[info_set_key][action] + opponent_reach * regret);
        }
    }

    // Update running weighted average strategy for ALL players (not just traversing player)
    // In vanilla CFR with full exploration, both players' average strategies should be accumulated
    // Quadratic weighting gives more weight to later iterations, improving convergence
    double my_reach = (current_player == 1) ? reach_p1 : reach_p2;
    double new_weight = current_iteration * current_iteration * my_reach;
    double old_weight = total_weight[info_set_key];
    total_weight[info_set_key] = old_weight + new_weight;

    // Only update average strategy if total_weight is non-zero (avoid NaN from division by zero)
    if (total_weight[info_set_key] > 1e-10) {
        for (const auto& action : actions) {
            // Running weighted average: (old_avg * old_weight + new_value * new_weight) / total_weight
            avg_strategy[info_set_key][action] =
                (avg_strategy[info_set_key][action] * old_weight + strategy[action] * new_weight)
                / total_weight[info_set_key];
        }
    }

    return expected_utility;
}

double CFRTrainer::cfr_external_sampling(GameState& state, int traversing_player,
                                         double reach_p1, double reach_p2, double sample_prob) {

    // Base case: terminal node
    if (state.is_terminal()) {
        return state.get_utility(1);  // Always return utility for player 1
    }

    // Get current state information
    int current_player = state.current_player;
    uint64_t info_set_key = state.get_info_set_key(current_player);
    ActionList actions = state.get_legal_actions();

    // Get current strategy for this information set
    auto strategy = get_strategy(info_set_key, actions);

    // TRAVERSING PLAYER: Explore all actions (like vanilla CFR)
    if (current_player == traversing_player) {
        // Compute utilities for each action
        std::unordered_map<GameAction, double, GameActionHash> action_utilities;
        double expected_utility = 0.0;

        for (const auto& action : actions) {
            // Apply action to get next state
            GameState next_state = apply_action(state, action);

            // Recursively compute utility with updated reach probabilities
            double utility;
            if (current_player == 1) {
                utility = cfr_external_sampling(next_state, traversing_player,
                                               reach_p1 * strategy[action], reach_p2,
                                               sample_prob);
            } else {
                utility = cfr_external_sampling(next_state, traversing_player,
                                               reach_p1, reach_p2 * strategy[action],
                                               sample_prob);
            }

            action_utilities[action] = utility;
            expected_utility += strategy[action] * utility;
        }

        // Update regrets with importance sampling correction
        double opponent_reach = (current_player == 1) ? reach_p2 : reach_p1;

        for (const auto& action : actions) {
            double regret = action_utilities[action] - expected_utility;

            // CFR+ update: add regret (scaled by 1/sample_prob) and floor to 0
            // The 1/sample_prob provides unbiased estimates when opponent actions are sampled
            regret_sum[info_set_key][action] = std::max(0.0,
                regret_sum[info_set_key][action] + (opponent_reach / sample_prob) * regret);
        }

        // Update running weighted average strategy with importance sampling correction
        double my_reach = (current_player == 1) ? reach_p1 : reach_p2;
        double new_weight = current_iteration * current_iteration * my_reach / sample_prob;
        double old_weight = total_weight[info_set_key];
        total_weight[info_set_key] = old_weight + new_weight;

        if (total_weight[info_set_key] > 1e-10) {
            for (const auto& action : actions) {
                avg_strategy[info_set_key][action] =
                    (avg_strategy[info_set_key][action] * old_weight + strategy[action] * new_weight)
                    / total_weight[info_set_key];
            }
        }

        return expected_utility;
    }
    // OPPONENT: Sample one action to reduce branching
    else {
        // Use thread_local static RNG for efficient random sampling
        thread_local static std::mt19937 gen(std::random_device{}());

        // Sample action according to strategy
        std::array<double, 5> probs;
        for (uint8_t i = 0; i < actions.count; i++) {
            probs[i] = strategy[actions.actions[i]];
        }

        std::discrete_distribution<> dist(probs.begin(), probs.begin() + actions.count);
        int sampled_idx = dist(gen);
        GameAction sampled_action = actions.actions[sampled_idx];
        double action_prob = strategy[sampled_action];

        // Apply sampled action and recurse
        GameState next_state = apply_action(state, sampled_action);

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

        // Update average strategy for opponent (even though we're sampling)
        // This contributes to the empirical frequency of play along this sampled path
        double my_reach = (current_player == 1) ? reach_p1 : reach_p2;
        double new_weight = current_iteration * current_iteration * my_reach / sample_prob;
        double old_weight = total_weight[info_set_key];
        total_weight[info_set_key] = old_weight + new_weight;

        if (total_weight[info_set_key] > 1e-10) {
            for (const auto& action : actions) {
                avg_strategy[info_set_key][action] =
                    (avg_strategy[info_set_key][action] * old_weight + strategy[action] * new_weight)
                    / total_weight[info_set_key];
            }
        }

        return utility;
    }
}

double CFRTrainer::rollout(GameState state) {
    // Monte Carlo rollout: sample actions until terminal state
    // Use current strategy to guide sampling (outcome sampling)

    // Use thread_local static RNG to avoid expensive re-initialization
    // Initialized once per thread, reused across all calls
    thread_local static std::mt19937 gen(std::random_device{}());

    // Debug: Track rollout count
    // thread_local static int rollout_count = 0;
    // rollout_count++;
    // if (rollout_count % 10000 == 0) {
    //     std::cout << "[DEBUG] Rollouts completed: " << rollout_count << "\n";
    // }
    //
    // Safety counter to prevent infinite loops
    // int rollout_steps = 0;
    // constexpr int MAX_ROLLOUT_STEPS = 100;

    while (!state.is_terminal()) {
        // Safety check: prevent infinite loops
        // if (rollout_steps >= MAX_ROLLOUT_STEPS) {
        //     // Rollout exceeded maximum steps - return heuristic evaluation
        //     int p1_score = state.p1_influence_count * 20 + state.p1_coins;
        //     int p2_score = state.p2_influence_count * 20 + state.p2_coins;
        //     // std::cerr << "[WARNING] Rollout exceeded " << MAX_ROLLOUT_STEPS << " steps. State: depth="
        //     //           << state.depth << " p1_inf=" << (int)state.p1_influence_count
        //     //           << " p2_inf=" << (int)state.p2_influence_count << "\n";
        //     return (p1_score - p2_score) / 50.0f;
        // }
        // rollout_steps++;

        int current_player = state.current_player;
        uint64_t info_set_key = state.get_info_set_key(current_player);
        ActionList actions = state.get_legal_actions();

        // Get strategy for this info set
        auto strategy = get_strategy(info_set_key, actions);

        // Sample an action according to the strategy
        std::discrete_distribution<> dist;
        std::array<double, 5> probs;  // Support up to 5 actions (INCOME, TAX, STEAL, ASSASSINATE, COUP)

        for (uint8_t i = 0; i < actions.count; i++) {
            probs[i] = strategy[actions.actions[i]];
        }

        dist = std::discrete_distribution<>(probs.begin(), probs.begin() + actions.count);
        int sampled_idx = dist(gen);
        GameAction sampled_action = actions.actions[sampled_idx];

        // Apply the sampled action
        state = apply_action(state, sampled_action);
    }

    // Return utility for player 1
    return state.get_utility(1);
}

void CFRTrainer::save_strategy(const std::string& filename) const {
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
        // Check if any action has NaN probability
        bool has_nan = false;
        for (const auto& [action, prob] : action_map) {
            if (std::isnan(prob)) {
                has_nan = true;
                nan_count++;
            }
        }

        // Skip info sets with NaN values
        if (has_nan) {
            skipped_infosets++;
            continue;
        }

        if (!first_infoset) {
            file << ",\n";
        }
        first_infoset = false;

        // Write key as hex string for readability
        file << "  \"0x" << std::hex << info_set_key << std::dec << "\": {\n";

        // Write probabilities (already normalized in running average)
        bool first_action = true;
        for (const auto& [action, prob] : action_map) {
            if (!first_action) {
                file << ",\n";
            }
            first_action = false;

            file << "    \"" << game_action_to_string(action) << "\": " << prob;
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

void CFRTrainer::load_strategy(const std::string& filename) {
    // TODO: Load strategy from JSON file (optional for now)
    // This is more complex - implement after save_strategy works

    std::cerr << "Warning: load_strategy() not yet implemented\n";
    (void)filename; // Suppress unused parameter warning
}
