#include "cfr_trainer.h"
#include <iostream>
#include <string>

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -v, --variant NAME    Game variant (base|simple|simpleblocking|full, default: base)\n";
    std::cout << "  -i, --iterations N    Number of training iterations (default: 10000)\n";
    std::cout << "  -d, --depth N         Maximum depth before external sampling (default: 20)\n";
    std::cout << "  --alpha N             DCFR alpha parameter (default: 1.5)\n";
    std::cout << "  --beta N              DCFR beta parameter (default: 0.0)\n";
    std::cout << "  --gamma N             DCFR gamma parameter (default: 2.0)\n";
    std::cout << "  --decay               Enable quadratic utility decay (encourages shorter games)\n";
    std::cout << "  --decay-alpha N       Decay strength parameter (default: 0.6, range: 0.0-1.0)\n";
    std::cout << "  --exploit-interval N  Number of iterations between tracking exploitability.\n";
    std::cout << "  -o, --output FILE     Output filename (default: {variant}_strategy_{depth}_{iterations}.json)\n";
    std::cout << "  -h, --help            Show this help message\n\n";
    std::cout << "Variants:\n";
    std::cout << "  base             - Base Coup (2 influences, 3 card types)\n";
    std::cout << "  simple           - Simple Coup (1 influence, with Assassinate)\n";
    std::cout << "  simpleblocking   - Simple Coup with Blocking (1 influence, 4 card types, blocking)\n";
    std::cout << "  full             - Full Coup (2 influences, 5 card types)\n\n";
    std::cout << "Utility Decay:\n";
    std::cout << "  Quadratic decay formula: utility × (1 - (α × depth/DEPTH_LIMIT)²)\n";
    std::cout << "  Higher alpha = more aggressive penalty for long games\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << program_name << " --variant base --iterations 50000 --depth 18\n";
    std::cout << "  " << program_name << " --variant simple -i 10000 -d 20 --decay --decay-alpha 0.7\n";
}

template<typename Rules>
void train_variant(int iterations, int max_depth, const std::string& output_file,
                   double alpha, double beta, double gamma, int exploit_interval) {
    CFRTrainer<Rules> trainer;
    trainer.set_max_depth(max_depth);
    trainer.set_dcfr_params(alpha, beta, gamma);
    trainer.train(iterations, exploit_interval);
    trainer.save_strategy(output_file + ".json");
    trainer.save_convergence_data(output_file + ".csv");
}

int main(int argc, char* argv[]) {
    std::string variant = "base";
    int iterations = 10000;
    int max_depth = 20;
    double alpha = 1.5, beta = 0.0, gamma = 2.0;
    bool enable_decay = false;
    double decay_alpha = 0.6;
    int exploit_interval = 100;
    std::string output_file = "";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        else if (arg == "-v" || arg == "--variant") {
            if (i + 1 < argc) {
                variant = argv[++i];
                if (variant != "base" && variant != "simple" && variant != "simpleblocking" && variant != "full") {
                    std::cerr << "Error: Unknown variant '" << variant << "'\n";
                    return 1;
                }
            } else {
                std::cerr << "Error: " << arg << " requires an argument\n";
                return 1;
            }
        }
        else if (arg == "-i" || arg == "--iterations") {
            if (i + 1 < argc) iterations = std::stoi(argv[++i]);
            else { std::cerr << "Error: " << arg << " requires an argument\n"; return 1; }
        }
        else if (arg == "-d" || arg == "--depth") {
            if (i + 1 < argc) max_depth = std::stoi(argv[++i]);
            else { std::cerr << "Error: " << arg << " requires an argument\n"; return 1; }
        }
        else if (arg == "--alpha") {
            if (i + 1 < argc) alpha = std::stod(argv[++i]);
            else { std::cerr << "Error: " << arg << " requires an argument\n"; return 1; }
        }
        else if (arg == "--beta") {
            if (i + 1 < argc) beta = std::stod(argv[++i]);
            else { std::cerr << "Error: " << arg << " requires an argument\n"; return 1; }
        }
        else if (arg == "--gamma") {
            if (i + 1 < argc) gamma = std::stod(argv[++i]);
            else { std::cerr << "Error: " << arg << " requires an argument\n"; return 1; }
        }
        else if (arg == "--decay") {
            enable_decay = true;
        }
        else if (arg == "--decay-alpha") {
            if (i + 1 < argc) {
                decay_alpha = std::stod(argv[++i]);
                if (decay_alpha < 0.0 || decay_alpha > 1.0) {
                    std::cerr << "Error: --decay-alpha must be in range [0.0, 1.0]\n";
                    return 1;
                }
            } else {
                std::cerr << "Error: " << arg << " requires an argument\n";
                return 1;
            }
        }
        else if (arg == "--exploit-interval") {
            if (i + 1 < argc) exploit_interval = std::stoi(argv[++i]);
            else { std::cerr << "Error: " << arg << " requires an argument\n"; return 1; }
        }
        else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) output_file = argv[++i];
            else { std::cerr << "Error: " << arg << " requires an argument\n"; return 1; }
        }
        else {
            std::cerr << "Error: Unknown argument '" << arg << "'\n";
            return 1;
        }
    }

    if (output_file.empty()) {
        output_file = variant + "_strategy_" + std::to_string(max_depth) + "_" + std::to_string(iterations);
    }

    // Configure utility decay
    auto& decay_config = UtilityDecayConfig::instance();
    decay_config.enabled = enable_decay;
    decay_config.alpha = decay_alpha;

    std::cout << "Variant: " << variant << " | Depth: " << max_depth << " | Iterations: " << iterations << "\n";
    std::cout << "DCFR Params: alpha=" << alpha << ", beta=" << beta << ", gamma=" << gamma << "\n";
    if (enable_decay) {
        std::cout << "Utility Decay: ENABLED (alpha=" << decay_alpha << ")\n";
    } else {
        std::cout << "Utility Decay: DISABLED\n";
    }
    std::cout << "Output: " << output_file << "\n\n";

    if (variant == "base") {
        train_variant<BaseCoupRules>(iterations, max_depth, output_file, alpha, beta, gamma, exploit_interval);
    } else if (variant == "simple") {
        train_variant<SimpleCoupRules>(iterations, max_depth, output_file, alpha, beta, gamma, exploit_interval);
    } else if (variant == "simpleblocking") {
        train_variant<SimpleCoupBlockingRules>(iterations, max_depth, output_file, alpha, beta, gamma, exploit_interval);
    } else if (variant == "full") {
        train_variant<FullCoupRules>(iterations, max_depth, output_file, alpha, beta, gamma, exploit_interval);
    }

    return 0;
}
