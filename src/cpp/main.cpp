#include "cfr_trainer.h"
#include <iostream>
#include <string>
#include <cstring>

// Helper function to parse command line arguments
void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -i, --iterations N    Number of training iterations (default: 10000)\n";
    std::cout << "  -r, --rollouts N      Number of Monte Carlo rollouts at depth limit (default: 10)\n";
    std::cout << "  -d, --depth N         Maximum depth before Monte Carlo rollouts (default: 14)\n";
    std::cout << "  -h, --help            Show this help message\n\n";
    std::cout << "Output file format: coup_strategy_{depth}_{rollouts}_{iterations}.json\n\n";
    std::cout << "Example:\n";
    std::cout << "  " << program_name << " --iterations 50000 --rollouts 24 --depth 16\n";
}

int main(int argc, char* argv[]) {
    // Default parameters
    int iterations = 10000;
    int num_rollouts = 10;
    int max_depth = 14;
    std::string output_file = "";  // Will be constructed from parameters if not specified

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
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
        else {
            std::cerr << "Error: Unknown argument '" << arg << "'\n";
            std::cerr << "Use --help for usage information\n";
            return 1;
        }
    }

    // Construct default output filename if not specified
    if (output_file.empty()) {
        output_file = "coup_strategy_" + std::to_string(max_depth) + "_" +
                      std::to_string(num_rollouts) + "_" + std::to_string(iterations) + ".json";
    }

    std::cout << "Coup CFR+ Trainer\n";
    std::cout << "=================\n\n";
    std::cout << "Configuration:\n";
    std::cout << "  Iterations:  " << iterations << "\n";
    std::cout << "  Rollouts:    " << num_rollouts << "\n";
    std::cout << "  Max Depth:   " << max_depth << "\n";

    // Create and configure trainer
    CFRTrainer trainer;
    trainer.set_rollout_count(num_rollouts);
    trainer.set_max_depth(max_depth);

    std::cout << "  Output:      " << output_file << "\n\n";

    // Train
    trainer.train(iterations);

    // Save strategy
    trainer.save_strategy(output_file);

    std::cout << "\nTraining complete! Strategy saved to " << output_file << "\n";
    std::cout << "You can now use this strategy to play against.\n";

    return 0;
}
