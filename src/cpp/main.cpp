#include "cfr_trainer.h"
#include <iostream>
#include <string>

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -v, --variant NAME    Game variant (base|simple|simpleassassin|full, default: base)\n";
    std::cout << "  -i, --iterations N    Number of training iterations (default: 10000)\n";
    std::cout << "  -d, --depth N         Maximum depth before external sampling (default: 20)\n";
    std::cout << "  --alpha N             DCFR alpha parameter (default: 1.5)\n";
    std::cout << "  --beta N              DCFR beta parameter (default: 0.0)\n";
    std::cout << "  --gamma N             DCFR gamma parameter (default: 2.0)\n";
    std::cout << "  -e, --exploit N       Estimate exploitability with N samples after training (default: 0 = off)\n";
    std::cout << "  --exploit-interval N  Track exploitability every N iterations (default: 0 = off)\n";
    std::cout << "  --exploit-samples N   Samples per exploitability check during training (default: 100)\n";
    std::cout << "  -o, --output FILE     Output filename (default: {variant}_strategy_{depth}_{iterations}.json)\n";
    std::cout << "  -h, --help            Show this help message\n\n";
    std::cout << "Variants:\n";
    std::cout << "  base          - Base Coup (2 influences, 3 card types)\n";
    std::cout << "  simple        - Simple Coup (1 influence, no Assassinate)\n";
    std::cout << "  simpleassassin - Simple Coup with Assassinate\n";
    std::cout << "  full          - Full Coup (2 influences, 5 card types)\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << program_name << " --variant base --iterations 50000 --depth 18\n";
    std::cout << "  " << program_name << " --variant simpleassassin -i 10000 -d 20\n";
}

template<typename Rules>
void train_variant(int iterations, int max_depth, const std::string& output_file,
                   double alpha, double beta, double gamma,
                   int exploit_final, int exploit_interval, int exploit_samples) {
    CFRTrainer<Rules> trainer;
    trainer.set_max_depth(max_depth);
    trainer.set_dcfr_params(alpha, beta, gamma);
    trainer.train(iterations, exploit_interval, exploit_samples);
    trainer.save_strategy(output_file);

    // Save convergence data if we tracked exploitability
    if (exploit_interval > 0) {
        std::string conv_file = output_file.substr(0, output_file.rfind('.')) + "_convergence.csv";
        trainer.save_convergence_data(conv_file);
    }

    // Final exploitability estimate
    if (exploit_final > 0) {
        trainer.estimate_exploitability(exploit_final);
    }
}

int main(int argc, char* argv[]) {
    std::string variant = "base";
    int iterations = 10000;
    int max_depth = 20;
    double alpha = 1.5, beta = 0.0, gamma = 2.0;
    int exploit_final = 0;
    int exploit_interval = 0;
    int exploit_samples = 100;
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
                if (variant != "base" && variant != "simple" && variant != "simpleassassin" && variant != "full") {
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
        else if (arg == "-e" || arg == "--exploit") {
            if (i + 1 < argc) exploit_final = std::stoi(argv[++i]);
            else { std::cerr << "Error: " << arg << " requires an argument\n"; return 1; }
        }
        else if (arg == "--exploit-interval") {
            if (i + 1 < argc) exploit_interval = std::stoi(argv[++i]);
            else { std::cerr << "Error: " << arg << " requires an argument\n"; return 1; }
        }
        else if (arg == "--exploit-samples") {
            if (i + 1 < argc) exploit_samples = std::stoi(argv[++i]);
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
        output_file = variant + "_strategy_" + std::to_string(max_depth) + "_" + std::to_string(iterations) + ".json";
    }

    std::cout << "Variant: " << variant << " | Depth: " << max_depth << " | Iterations: " << iterations << "\n";
    std::cout << "Output: " << output_file << "\n";

    if (variant == "base") {
        train_variant<BaseCoupRules>(iterations, max_depth, output_file, alpha, beta, gamma, exploit_final, exploit_interval, exploit_samples);
    } else if (variant == "simple") {
        train_variant<SimpleCoupRules>(iterations, max_depth, output_file, alpha, beta, gamma, exploit_final, exploit_interval, exploit_samples);
    } else if (variant == "simpleassassin") {
        train_variant<SimpleAssassinCoupRules>(iterations, max_depth, output_file, alpha, beta, gamma, exploit_final, exploit_interval, exploit_samples);
    } else if (variant == "full") {
        train_variant<FullCoupRules>(iterations, max_depth, output_file, alpha, beta, gamma, exploit_final, exploit_interval, exploit_samples);
    }

    return 0;
}
