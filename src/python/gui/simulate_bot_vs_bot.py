"""
Bot vs Bot simulation for Coup.

Simulate games between two bot players and collect statistics.
"""

import argparse
import json
from typing import Dict, List, Tuple
from game_engine import GameEngine
from bot_player import BotPlayer
from tqdm import tqdm


class BotVsBotSimulator:
    """Simulate games between two bots."""

    def __init__(self, variant: str, bot1_strategy: str, bot2_strategy: str):
        """
        Initialize simulator.

        Args:
            variant: Game variant
            bot1_strategy: Path to bot 1 strategy JSON
            bot2_strategy: Path to bot 2 strategy JSON
        """
        self.variant = variant
        self.engine = GameEngine(variant)
        self.bot1 = BotPlayer(bot1_strategy, variant, 1)
        self.bot2 = BotPlayer(bot2_strategy, variant, 2)

    def simulate_game(self, verbose: bool = False) -> int:
        """
        Simulate a single game.

        Args:
            verbose: Print game progress

        Returns:
            Winner (1 or 2)
        """
        state = self.engine.create_initial_state()

        if verbose:
            print(f"\n=== Starting {self.variant.upper()} Game ===")
            print(f"Bot 1 cards: {[inf.name for inf in state.p1_influences]}")
            print(f"Bot 2 cards: {[inf.name for inf in state.p2_influences]}")
            print()

        turn = 0
        while not self.engine.is_game_over(state):
            turn += 1
            current_player = state.current_player

            # Get action from appropriate bot
            if current_player == 1:
                action = self.bot1.get_action(state)
                bot_name = "Bot 1"
            else:
                action = self.bot2.get_action(state)
                bot_name = "Bot 2"

            if verbose:
                action_name = str(action).split('.')[-1]
                print(f"Turn {turn} - {bot_name}: {action_name}")

            # Apply action
            state = self.engine.apply_action(state, action)

            # Safety check for infinite loops
            if turn > 1000:
                print("Warning: Game exceeded 1000 turns, ending")
                break

        winner = self.engine.get_winner(state)
        if verbose:
            print(f"\n=== Game Over: Bot {winner} wins! ===\n")

        return winner

    def simulate_multiple(self, num_games: int, verbose: bool = False) -> Dict:
        """
        Simulate multiple games and collect statistics.

        Args:
            num_games: Number of games to simulate
            verbose: Print progress

        Returns:
            Statistics dictionary
        """
        bot1_wins = 0
        bot2_wins = 0

        print(f"Simulating {num_games} games of {self.variant}...")

        for i in tqdm(range(num_games)):
            # if verbose or (i + 1) % 10 == 0:
            #     print(f"Game {i + 1}/{num_games}")

            winner = self.simulate_game(verbose=verbose)

            if winner == 1:
                bot1_wins += 1
            else:
                bot2_wins += 1

        stats = {
            "variant": self.variant,
            "total_games": num_games,
            "bot1_wins": bot1_wins,
            "bot2_wins": bot2_wins,
            "bot1_win_rate": bot1_wins / num_games,
            "bot2_win_rate": bot2_wins / num_games,
        }

        return stats


def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(description="Simulate bot vs bot Coup games")
    parser.add_argument("--variant", type=str, required=True, choices=["simple", "simpleblocking", "base"],
                        help="Game variant")
    parser.add_argument("--bot1", type=str, required=True, help="Path to bot 1 strategy JSON")
    parser.add_argument("--bot2", type=str, required=True, help="Path to bot 2 strategy JSON")
    parser.add_argument("--games", type=int, default=100, help="Number of games to simulate")
    parser.add_argument("--verbose", action="store_true", help="Print detailed game logs")
    parser.add_argument("--output", type=str, help="Save statistics to JSON file")

    args = parser.parse_args()

    # Run simulation
    simulator = BotVsBotSimulator(args.variant, args.bot1, args.bot2)
    stats = simulator.simulate_multiple(args.games, verbose=args.verbose)

    # Print results
    print("\n" + "=" * 60)
    print("SIMULATION RESULTS")
    print("=" * 60)
    print(f"Variant: {stats['variant']}")
    print(f"Total Games: {stats['total_games']}")
    print(f"Bot 1 Wins: {stats['bot1_wins']} ({stats['bot1_win_rate']:.1%})")
    print(f"Bot 2 Wins: {stats['bot2_wins']} ({stats['bot2_win_rate']:.1%})")
    print("=" * 60)

    # Save to file if requested
    if args.output:
        with open(args.output, 'w') as f:
            json.dump(stats, f, indent=2)
        print(f"\nStatistics saved to {args.output}")


if __name__ == "__main__":
    main()
