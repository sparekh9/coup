#include "cfr_trainer.h"
#include <iostream>
#include <string>
#include <cstring>

// Helper function to parse command line arguments
void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -v, --variant NAME    Game variant (base|simple|simpleassassin|full, default: base)\n";
    std::cout << "  -i, --iterations N    Number of training iterations (default: 10000)\n";
    std::cout << "  -r, --rollouts N      Number of Monte Carlo rollouts at depth limit (default: 10)\n";
    std::cout << "  -d, --depth N         Maximum depth before switching to external sampling (default: 14)\n";
    std::cout << "  --debug [ITER]        Enable debug mode (optionally for specific iteration)\n";
    std::cout << "  --debug-states N      Max states to print in debug mode (default: 10)\n";
    std::cout << "  -h, --help            Show this help message\n\n";
    std::cout << "Variants:\n";
    std::cout << "  base          - Base Coup (2 influences, 3 card types: Duke/Captain/Assassin)\n";
    std::cout << "  simple        - Simple Coup (1 influence, 3 card types, no Assassinate action)\n";
    std::cout << "  simpleassassin - Simple Assassin Coup (1 influence, 3 card types, WITH Assassinate)\n";
    std::cout << "  full          - Full Coup (2 influences, 5 card types, all actions)\n\n";
    std::cout << "Output file format: {variant}_strategy_{depth}_{rollouts}_{iterations}.json\n\n";
    std::cout << "Debug Mode:\n";
    std::cout << "  --debug           - Enable debug for ALL iterations (LOTS of output!)\n";
    std::cout << "  --debug 5         - Enable debug only for iteration 5\n";
    std::cout << "  --debug-states 20 - Show details for up to 20 states per iteration\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << program_name << " --variant base --iterations 50000 --rollouts 24 --depth 16\n";
    std::cout << "  " << program_name << " --variant base --iterations 100 --debug 1\n";
    std::cout << "  " << program_name << " --variant simple --iterations 10 --debug --debug-states 5\n";
}

// Template function to train a specific variant
template<typename Rules>
void train_variant(int iterations, int num_rollouts, int max_depth, const std::string& output_file,
                   bool debug_enabled, int debug_iteration, int max_debug_states) {
    std::cout << "Coup CFR+ Trainer\n";
    std::cout << "=================\n\n";
    std::cout << "Configuration:\n";
    std::cout << "  Iterations:  " << iterations << "\n";
    std::cout << "  Rollouts:    " << num_rollouts << "\n";
    std::cout << "  Max Depth:   " << max_depth << "\n";
    std::cout << "  Output:      " << output_file << "\n";
    if (debug_enabled) {
        std::cout << "  Debug:       Enabled";
        if (debug_iteration >= 0) {
            std::cout << " (iteration " << debug_iteration << ")";
        } else {
            std::cout << " (ALL iterations)";
        }
        std::cout << "\n";
        std::cout << "  Debug states:" << max_debug_states << "\n";
    }
    std::cout << "\n";

    // Create and configure trainer
    CFRTrainer<Rules> trainer;
    trainer.set_rollout_count(num_rollouts);
    trainer.set_max_depth(max_depth);

    // Configure debug mode
    if (debug_enabled) {
        trainer.enable_debug(true);
        trainer.set_debug_iteration(debug_iteration);
        trainer.set_max_debug_states(max_debug_states);
    }

    // Train
    trainer.train(iterations);

    // Save strategy
    trainer.save_strategy(output_file);

    std::cout << "\nTraining complete! Strategy saved to " << output_file << "\n";
    std::cout << "You can now use this strategy to play against.\n";
}

int main(int argc, char* argv[]) {
    // Default parameters
    std::string variant = "base";
    int iterations = 10000;
    int num_rollouts = 10;
    int max_depth = 14;
    std::string output_file = "";  // Will be constructed from parameters if not specified
    bool debug_enabled = false;
    int debug_iteration = -1;  // -1 means all iterations
    int max_debug_states = 10;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        else if (arg == "-v" || arg == "--variant") {
            if (i + 1 < argc) {
                variant = argv[++i];
                if (variant != "base" && variant != "simple" && variant != "simpleassassin" && variant != "full") {
                    std::cerr << "Error: Unknown variant '" << variant << "'\n";
                    std::cerr << "Valid variants: base, simple, simpleassassin, full\n";
                    return 1;
                }
            } else {
                std::cerr << "Error: " << arg << " requires an argument\n";
                return 1;
            }
        }
        else if (arg == "-i" || arg == "--iterations") {
            if (i + 1 < argc) {
                iterations = std::stoi(argv[++i]);
            } else {
                std::cerr << "Error: " << arg << " requires an argument\n";
                return 1;
            }
        }
        else if (arg == "-r" || arg == "--rollouts") {
            if (i + 1 < argc) {
                num_rollouts = std::stoi(argv[++i]);
            } else {
                std::cerr << "Error: " << arg << " requires an argument\n";
                return 1;
            }
        }
        else if (arg == "-d" || arg == "--depth") {
            if (i + 1 < argc) {
                max_depth = std::stoi(argv[++i]);
            } else {
                std::cerr << "Error: " << arg << " requires an argument\n";
                return 1;
            }
        }
        else if (arg == "--debug") {
            debug_enabled = true;
            // Check if next arg is a number (specific iteration)
            if (i + 1 < argc) {
                try {
                    debug_iteration = std::stoi(argv[i + 1]);
                    i++;  // Consume the iteration number
                } catch (...) {
                    // Not a number, use default (all iterations)
                    debug_iteration = -1;
                }
            }
        }
        else if (arg == "--debug-states") {
            if (i + 1 < argc) {
                max_debug_states = std::stoi(argv[++i]);
            } else {
                std::cerr << "Error: " << arg << " requires an argument\n";
                return 1;
            }
        }
        else {
            std::cerr << "Error: Unknown argument '" << arg << "'\n";
            std::cerr << "Use --help for usage information\n";
            return 1;
        }
    }

    // Construct default output filename if not specified
    if (output_file.empty()) {
        output_file = variant + "_strategy_" + std::to_string(max_depth) + "_" +
                      std::to_string(num_rollouts) + "_" + std::to_string(iterations) + ".json";
    }

    // Train the selected variant
    if (variant == "base") {
        train_variant<BaseCoupRules>(iterations, num_rollouts, max_depth, output_file,
                                     debug_enabled, debug_iteration, max_debug_states);
    }
    else if (variant == "simple") {
        train_variant<SimpleCoupRules>(iterations, num_rollouts, max_depth, output_file,
                                       debug_enabled, debug_iteration, max_debug_states);
    }
    else if (variant == "simpleassassin") {
        train_variant<SimpleAssassinCoupRules>(iterations, num_rollouts, max_depth, output_file,
                                               debug_enabled, debug_iteration, max_debug_states);
    }
    else if (variant == "full") {
        train_variant<FullCoupRules>(iterations, num_rollouts, max_depth, output_file,
                                     debug_enabled, debug_iteration, max_debug_states);
    }

    return 0;
}
