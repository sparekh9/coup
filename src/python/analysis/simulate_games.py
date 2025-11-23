#!/usr/bin/env python3
"""
Bot vs Bot Simulation Framework for Coup CFR Strategies

Usage:
    # Compare two strategies head-to-head
    python simulate_games.py strategy1.json strategy2.json --games 1000

    # Run a tournament between multiple strategies
    python simulate_games.py strategy1.json strategy2.json strategy3.json --tournament --games 500

    # Compare with specific variant (auto-detected from filename by default)
    python simulate_games.py strategy1.json strategy2.json --variant simpleassassin

    # Verbose mode with game-by-game output
    python simulate_games.py strategy1.json strategy2.json --games 100 --verbose

    # Compute game value (self-play expected utility)
    python simulate_games.py strategy.json --game-value --games 10000

    # Estimate exploitability against best response
    python simulate_games.py strategy.json --exploitability --games 1000
"""

import os
import sys
import json
import random
import argparse
from pathlib import Path
from typing import Dict, List, Tuple, Optional
from dataclasses import dataclass
from itertools import combinations
import math

# Add gui directory to path
sys.path.insert(0, os.path.join(str(Path(__file__).parent.parent), 'gui'))

from game_state import (
    SimpleGameState, ChallengeResponse,
    apply_action, create_initial_state,
    initialize_variant
)
import game_state


@dataclass
class GameResult:
    """Result of a single game"""
    winner: int  # 1 or 2 (0 for draw)
    turns: int
    p1_final_influences: int
    p2_final_influences: int
    p1_final_coins: int
    p2_final_coins: int


@dataclass
class MatchResult:
    """Aggregated results from multiple games"""
    bot1_name: str
    bot2_name: str
    games_played: int
    bot1_wins: int
    bot2_wins: int
    draws: int
    avg_turns: float
    bot1_win_rate: float
    bot2_win_rate: float
    # First-player advantage analysis
    p1_wins_as_first: int
    p1_games_as_first: int
    p2_wins_as_first: int
    p2_games_as_first: int


class BotStrategy:
    """Loads and uses CFR strategy from JSON file"""

    def __init__(self, strategy_file: str, bot_player: int = 1, silent: bool = False):
        self.strategy_file = strategy_file
        self.strategy: Dict[str, Dict[str, float]] = {}
        self.variant = "base"
        self.bot_player = bot_player
        self.silent = silent
        self.name = Path(strategy_file).stem
        self.load_strategy()
        self.detect_variant()

    def load_strategy(self):
        """Load strategy from JSON file"""
        try:
            with open(self.strategy_file, 'r') as f:
                self.strategy = json.load(f)
            if not self.silent:
                print(f"  Loaded '{self.name}' with {len(self.strategy)} information sets")
        except FileNotFoundError:
            print(f"Error: Strategy file '{self.strategy_file}' not found!")
            sys.exit(1)
        except json.JSONDecodeError as e:
            print(f"Error: Invalid JSON in strategy file: {e}")
            sys.exit(1)

    def detect_variant(self):
        """Detect game variant from filename"""
        filename = Path(self.strategy_file).stem.lower()
        if 'simpleassassin' in filename:
            self.variant = "simpleassassin"
        elif 'base' in filename:
            self.variant = "base"
        elif 'simple' in filename:
            self.variant = "simple"
        elif 'full' in filename:
            self.variant = "full"
        else:
            self.variant = "base"

    def get_action(self, state: SimpleGameState):
        """Get bot's action based on loaded strategy"""
        info_set_key = state.get_info_set_key(self.bot_player)

        if info_set_key not in self.strategy:
            # Strategy not found, use random legal action
            legal_actions = state.get_legal_actions(apply_pruning=False)
            return random.choice(legal_actions)

        action_probs = self.strategy[info_set_key]
        legal_actions = state.get_legal_actions(apply_pruning=False)
        action_dict = {}

        for action in legal_actions:
            action_name = action_to_string(action)
            if action_name in action_probs:
                action_dict[action] = action_probs[action_name]

        if not action_dict:
            return random.choice(legal_actions)

        # Normalize probabilities
        total = sum(action_dict.values())
        if total > 0:
            for action in action_dict:
                action_dict[action] /= total

        # Sample action based on probabilities
        actions = list(action_dict.keys())
        probs = list(action_dict.values())
        return random.choices(actions, weights=probs, k=1)[0]


class RandomBot:
    """A simple random bot for baseline comparison"""

    def __init__(self, bot_player: int = 1):
        self.bot_player = bot_player
        self.name = "RandomBot"
        self.variant = None  # Accepts any variant

    def get_action(self, state: SimpleGameState):
        """Choose a random legal action"""
        legal_actions = state.get_legal_actions(apply_pruning=False)
        return random.choice(legal_actions)


def action_to_string(action) -> str:
    """Convert action to string matching C++ format"""
    if isinstance(action, ChallengeResponse):
        return "PASS" if action == ChallengeResponse.PASS else "CHALLENGE"
    else:
        return action.name


def play_game(bot1, bot2, first_player: int = 1, verbose: bool = False) -> GameResult:
    """
    Play a single game between two bots.

    Args:
        bot1: First bot (configured as player 1)
        bot2: Second bot (configured as player 2)
        first_player: Who moves first (1 or 2)
        verbose: Print game actions

    Returns:
        GameResult with outcome
    """
    state = create_initial_state()

    # If first_player is 2, swap who moves first
    if first_player == 2:
        state.current_player = 2

    turn = 0
    max_turns = 200  # Safety limit

    while not state.is_terminal() and turn < max_turns:
        turn += 1

        if state.current_player == 1:
            action = bot1.get_action(state)
        else:
            action = bot2.get_action(state)

        if verbose:
            player_name = bot1.name if state.current_player == 1 else bot2.name
            print(f"  Turn {turn}: {player_name} plays {action_to_string(action)}")

        state = apply_action(state, action)

    # Determine winner
    utility = state.get_utility(1)
    if utility > 0:
        winner = 1
    elif utility < 0:
        winner = 2
    else:
        winner = 0  # Draw

    return GameResult(
        winner=winner,
        turns=turn,
        p1_final_influences=len(state.p1_influences),
        p2_final_influences=len(state.p2_influences),
        p1_final_coins=state.p1_coins,
        p2_final_coins=state.p2_coins
    )


def run_match(bot1, bot2, num_games: int, verbose: bool = False) -> MatchResult:
    """
    Run multiple games between two bots with alternating first player.

    Args:
        bot1: First bot strategy
        bot2: Second bot strategy
        num_games: Number of games to play
        verbose: Print each game result

    Returns:
        MatchResult with aggregated statistics
    """
    results: List[GameResult] = []
    bot1_wins = 0
    bot2_wins = 0
    draws = 0

    # Track first-player advantage
    p1_wins_as_first = 0
    p1_games_as_first = 0
    p2_wins_as_first = 0
    p2_games_as_first = 0

    for i in range(num_games):
        # Alternate first player
        first_player = (i % 2) + 1

        if first_player == 1:
            p1_games_as_first += 1
        else:
            p2_games_as_first += 1

        result = play_game(bot1, bot2, first_player=first_player, verbose=verbose)
        results.append(result)

        if result.winner == 1:
            bot1_wins += 1
            if first_player == 1:
                p1_wins_as_first += 1
        elif result.winner == 2:
            bot2_wins += 1
            if first_player == 2:
                p2_wins_as_first += 1
        else:
            draws += 1

        if verbose:
            winner_name = bot1.name if result.winner == 1 else (bot2.name if result.winner == 2 else "Draw")
            first_name = bot1.name if first_player == 1 else bot2.name
            print(f"Game {i+1}: {winner_name} wins (first player: {first_name}, {result.turns} turns)")

    avg_turns = sum(r.turns for r in results) / num_games if num_games > 0 else 0

    return MatchResult(
        bot1_name=bot1.name,
        bot2_name=bot2.name,
        games_played=num_games,
        bot1_wins=bot1_wins,
        bot2_wins=bot2_wins,
        draws=draws,
        avg_turns=avg_turns,
        bot1_win_rate=bot1_wins / num_games if num_games > 0 else 0,
        bot2_win_rate=bot2_wins / num_games if num_games > 0 else 0,
        p1_wins_as_first=p1_wins_as_first,
        p1_games_as_first=p1_games_as_first,
        p2_wins_as_first=p2_wins_as_first,
        p2_games_as_first=p2_games_as_first
    )


def wilson_score_interval(wins: int, total: int, confidence: float = 0.95) -> Tuple[float, float]:
    """
    Calculate Wilson score confidence interval for win rate.

    Args:
        wins: Number of wins
        total: Total games
        confidence: Confidence level (default 95%)

    Returns:
        (lower_bound, upper_bound) of win rate
    """
    if total == 0:
        return (0.0, 0.0)

    z = 1.96 if confidence == 0.95 else 2.576  # z-score for confidence level
    p = wins / total

    denominator = 1 + z*z / total
    center = (p + z*z / (2*total)) / denominator
    spread = z * math.sqrt((p*(1-p) + z*z/(4*total)) / total) / denominator

    return (max(0, center - spread), min(1, center + spread))


def print_match_result(result: MatchResult, show_confidence: bool = True):
    """Pretty print match results"""
    print("\n" + "="*70)
    print(f"MATCH RESULT: {result.bot1_name} vs {result.bot2_name}")
    print("="*70)

    print(f"\nGames played: {result.games_played}")
    print(f"Average game length: {result.avg_turns:.1f} turns")

    print(f"\n{result.bot1_name}:")
    print(f"  Wins: {result.bot1_wins} ({result.bot1_win_rate*100:.1f}%)")
    if show_confidence and result.games_played >= 30:
        lower, upper = wilson_score_interval(result.bot1_wins, result.games_played)
        print(f"  95% CI: [{lower*100:.1f}%, {upper*100:.1f}%]")

    print(f"\n{result.bot2_name}:")
    print(f"  Wins: {result.bot2_wins} ({result.bot2_win_rate*100:.1f}%)")
    if show_confidence and result.games_played >= 30:
        lower, upper = wilson_score_interval(result.bot2_wins, result.games_played)
        print(f"  95% CI: [{lower*100:.1f}%, {upper*100:.1f}%]")

    if result.draws > 0:
        print(f"\nDraws: {result.draws} ({result.draws/result.games_played*100:.1f}%)")

    # First player advantage analysis
    print("\nFirst Player Advantage:")
    if result.p1_games_as_first > 0:
        p1_first_rate = result.p1_wins_as_first / result.p1_games_as_first
        print(f"  {result.bot1_name} as P1: {result.p1_wins_as_first}/{result.p1_games_as_first} ({p1_first_rate*100:.1f}%)")
    if result.p2_games_as_first > 0:
        p2_first_rate = result.p2_wins_as_first / result.p2_games_as_first
        print(f"  {result.bot2_name} as P1: {result.p2_wins_as_first}/{result.p2_games_as_first} ({p2_first_rate*100:.1f}%)")

    print("="*70)


def run_tournament(strategies: List[str], num_games: int, variant: Optional[str] = None, verbose: bool = False):
    """
    Run a round-robin tournament between multiple strategies.

    Args:
        strategies: List of strategy file paths
        num_games: Games per matchup
        variant: Force variant (auto-detect if None)
        verbose: Print detailed output
    """
    print("\n" + "="*70)
    print("TOURNAMENT MODE")
    print("="*70)
    print(f"Strategies: {len(strategies)}")
    print(f"Games per matchup: {num_games}")
    print(f"Total games: {len(list(combinations(range(len(strategies)), 2))) * num_games}")

    # Load all strategies
    bots = []
    for strategy_file in strategies:
        bot = BotStrategy(strategy_file, bot_player=1, silent=True)
        bots.append(bot)

    # Determine variant
    if variant:
        initialize_variant(variant)
        print(f"Variant: {variant} (forced)")
    else:
        initialize_variant(bots[0].variant)
        print(f"Variant: {bots[0].variant} (auto-detected from first strategy)")

    print(f"\nLoaded strategies:")
    for bot in bots:
        print(f"  - {bot.name}")

    # Track tournament standings
    standings = {bot.name: {"wins": 0, "losses": 0, "draws": 0, "games": 0} for bot in bots}

    # Run all pairings
    all_results = []
    pairings = list(combinations(range(len(bots)), 2))

    print(f"\nRunning {len(pairings)} matchups...")

    for i, (idx1, idx2) in enumerate(pairings):
        bot1_template = bots[idx1]
        bot2_template = bots[idx2]

        # Create fresh instances with correct player assignments
        bot1 = BotStrategy(bot1_template.strategy_file, bot_player=1, silent=True)
        bot2 = BotStrategy(bot2_template.strategy_file, bot_player=2, silent=True)

        print(f"\n[{i+1}/{len(pairings)}] {bot1.name} vs {bot2.name}...", end=" ", flush=True)
        result = run_match(bot1, bot2, num_games, verbose=verbose)
        all_results.append(result)
        print(f"{result.bot1_wins}-{result.bot2_wins}")

        # Update standings
        standings[bot1.name]["wins"] += result.bot1_wins
        standings[bot1.name]["losses"] += result.bot2_wins
        standings[bot1.name]["draws"] += result.draws
        standings[bot1.name]["games"] += result.games_played

        standings[bot2.name]["wins"] += result.bot2_wins
        standings[bot2.name]["losses"] += result.bot1_wins
        standings[bot2.name]["draws"] += result.draws
        standings[bot2.name]["games"] += result.games_played

    # Print tournament results
    print("\n" + "="*70)
    print("TOURNAMENT STANDINGS")
    print("="*70)

    # Sort by win rate
    sorted_standings = sorted(
        standings.items(),
        key=lambda x: x[1]["wins"] / x[1]["games"] if x[1]["games"] > 0 else 0,
        reverse=True
    )

    print(f"\n{'Rank':<6}{'Strategy':<40}{'W-L-D':<15}{'Win Rate':<12}")
    print("-"*70)

    for rank, (name, stats) in enumerate(sorted_standings, 1):
        win_rate = stats["wins"] / stats["games"] * 100 if stats["games"] > 0 else 0
        record = f"{stats['wins']}-{stats['losses']}-{stats['draws']}"
        print(f"{rank:<6}{name:<40}{record:<15}{win_rate:.1f}%")

    # Print head-to-head matrix
    print("\n" + "="*70)
    print("HEAD-TO-HEAD RESULTS (row player's wins)")
    print("="*70)

    bot_names = [bot.name for bot in bots]
    # Truncate names for display
    short_names = [name[:12] for name in bot_names]

    # Header row
    print(f"\n{'':15}", end="")
    for name in short_names:
        print(f"{name:>13}", end="")
    print()

    # Create result matrix
    matrix = {name: {n: "-" for n in bot_names} for name in bot_names}
    for result in all_results:
        matrix[result.bot1_name][result.bot2_name] = str(result.bot1_wins)
        matrix[result.bot2_name][result.bot1_name] = str(result.bot2_wins)

    # Print matrix
    for name, short in zip(bot_names, short_names):
        print(f"{short:15}", end="")
        for opponent in bot_names:
            print(f"{matrix[name][opponent]:>13}", end="")
        print()

    print("="*70)

    return all_results


# ============================================================================
# GAME VALUE ANALYSIS
# ============================================================================

@dataclass
class GameValueResult:
    """Results from game value analysis"""
    strategy_name: str
    num_games: int
    # Expected utilities
    p1_expected_utility: float
    p2_expected_utility: float
    p1_utility_std: float
    p2_utility_std: float
    # Win rates
    p1_win_rate: float
    p2_win_rate: float
    draw_rate: float
    # First player advantage
    first_player_advantage: float  # P1 win rate - 0.5
    # Confidence intervals
    p1_utility_ci: Tuple[float, float]


def compute_game_value(strategy_file: str, num_games: int, variant: Optional[str] = None) -> GameValueResult:
    """
    Compute the game value through self-play.

    The game value is the expected utility when a strategy plays against itself.
    For a Nash equilibrium strategy:
    - In a symmetric game, game value ≈ 0 (fair game)
    - First player advantage shows if going first helps

    Args:
        strategy_file: Path to strategy JSON
        num_games: Number of self-play games
        variant: Force variant (auto-detect if None)

    Returns:
        GameValueResult with expected utilities and statistics
    """
    print("\n" + "="*70)
    print("GAME VALUE ANALYSIS (Self-Play)")
    print("="*70)

    # Load strategy for both players
    bot1 = BotStrategy(strategy_file, bot_player=1, silent=False)
    bot2 = BotStrategy(strategy_file, bot_player=2, silent=True)

    # Initialize variant
    if variant:
        initialize_variant(variant)
    else:
        initialize_variant(bot1.variant)

    print(f"Variant: {game_state.CURRENT_VARIANT.get_variant_name()}")
    print(f"Running {num_games} self-play games...")

    # Track utilities
    p1_utilities = []
    p2_utilities = []
    p1_wins = 0
    p2_wins = 0
    draws = 0

    # Track by starting position
    p1_wins_when_first = 0
    p1_games_as_first = 0

    for i in range(num_games):
        # Alternate who goes first
        first_player = (i % 2) + 1

        if first_player == 1:
            p1_games_as_first += 1

        result = play_game(bot1, bot2, first_player=first_player)

        # Get actual utilities (not just win/loss)
        # For terminal games, utility is +1/-1
        # For non-terminal (depth limit), utility is partial
        if result.winner == 1:
            p1_utilities.append(1.0)
            p2_utilities.append(-1.0)
            p1_wins += 1
            if first_player == 1:
                p1_wins_when_first += 1
        elif result.winner == 2:
            p1_utilities.append(-1.0)
            p2_utilities.append(1.0)
            p2_wins += 1
        else:
            p1_utilities.append(0.0)
            p2_utilities.append(0.0)
            draws += 1

    # Compute statistics
    p1_expected = sum(p1_utilities) / num_games
    p2_expected = sum(p2_utilities) / num_games

    p1_std = math.sqrt(sum((u - p1_expected)**2 for u in p1_utilities) / num_games) if num_games > 1 else 0
    p2_std = math.sqrt(sum((u - p2_expected)**2 for u in p2_utilities) / num_games) if num_games > 1 else 0

    # Confidence interval for expected utility (using t-distribution approximation)
    std_err = p1_std / math.sqrt(num_games) if num_games > 0 else 0
    ci_width = 1.96 * std_err
    p1_ci = (p1_expected - ci_width, p1_expected + ci_width)

    # Win rates
    p1_win_rate = p1_wins / num_games if num_games > 0 else 0
    p2_win_rate = p2_wins / num_games if num_games > 0 else 0
    draw_rate = draws / num_games if num_games > 0 else 0

    # First player advantage
    first_player_win_rate = p1_wins_when_first / p1_games_as_first if p1_games_as_first > 0 else 0.5
    first_player_advantage = first_player_win_rate - 0.5

    result = GameValueResult(
        strategy_name=bot1.name,
        num_games=num_games,
        p1_expected_utility=p1_expected,
        p2_expected_utility=p2_expected,
        p1_utility_std=p1_std,
        p2_utility_std=p2_std,
        p1_win_rate=p1_win_rate,
        p2_win_rate=p2_win_rate,
        draw_rate=draw_rate,
        first_player_advantage=first_player_advantage,
        p1_utility_ci=p1_ci
    )

    print_game_value_result(result)
    return result


def print_game_value_result(result: GameValueResult):
    """Pretty print game value results"""
    print(f"\nStrategy: {result.strategy_name}")
    print(f"Games: {result.num_games}")

    print(f"\n--- Expected Utility (Game Value) ---")
    print(f"Player 1: {result.p1_expected_utility:+.4f} (std: {result.p1_utility_std:.4f})")
    print(f"Player 2: {result.p2_expected_utility:+.4f} (std: {result.p2_utility_std:.4f})")
    print(f"95% CI for P1: [{result.p1_utility_ci[0]:+.4f}, {result.p1_utility_ci[1]:+.4f}]")

    print(f"\n--- Win Rates ---")
    print(f"Player 1 wins: {result.p1_win_rate*100:.1f}%")
    print(f"Player 2 wins: {result.p2_win_rate*100:.1f}%")
    if result.draw_rate > 0:
        print(f"Draws: {result.draw_rate*100:.1f}%")

    print(f"\n--- First Player Advantage ---")
    print(f"First player win rate: {(0.5 + result.first_player_advantage)*100:.1f}%")
    print(f"Advantage: {result.first_player_advantage*100:+.1f} percentage points")

    # Interpretation
    print(f"\n--- Interpretation ---")
    if abs(result.p1_expected_utility) < 0.05:
        print("Game value ~= 0: The game appears to be approximately fair.")
    elif result.p1_expected_utility > 0:
        print(f"Game value > 0: Player 1 has a {result.p1_expected_utility*100:.1f}% expected advantage.")
    else:
        print(f"Game value < 0: Player 2 has a {-result.p1_expected_utility*100:.1f}% expected advantage.")

    if result.first_player_advantage > 0.05:
        print(f"Significant first-player advantage detected ({result.first_player_advantage*100:.1f}%).")
    elif result.first_player_advantage < -0.05:
        print(f"Significant second-player advantage detected ({-result.first_player_advantage*100:.1f}%).")

    print("="*70)


class BestResponseBot:
    """
    A bot that tries to exploit a given strategy by always choosing
    the action that maximizes expected value against that strategy.

    This is an approximate best response using Monte Carlo sampling.
    """

    def __init__(self, opponent_strategy: BotStrategy, bot_player: int = 1, rollout_count: int = 50):
        self.opponent = opponent_strategy
        self.bot_player = bot_player
        self.opponent_player = 3 - bot_player
        self.name = f"BestResponse(vs {opponent_strategy.name})"
        self.rollout_count = rollout_count
        self.variant = opponent_strategy.variant

    def get_action(self, state: SimpleGameState):
        """
        Choose the action that maximizes expected utility through Monte Carlo rollouts.
        """
        legal_actions = state.get_legal_actions(apply_pruning=False)

        if len(legal_actions) == 1:
            return legal_actions[0]

        best_action = None
        best_value = float('-inf')

        for action in legal_actions:
            # Estimate value of this action through rollouts
            total_value = 0.0

            for _ in range(self.rollout_count):
                # Apply action
                next_state = apply_action(state, action)

                # Rollout to terminal using opponent's strategy for their moves
                value = self._rollout(next_state)
                total_value += value

            avg_value = total_value / self.rollout_count

            if avg_value > best_value:
                best_value = avg_value
                best_action = action

        return best_action

    def _rollout(self, state: SimpleGameState) -> float:
        """Rollout to terminal, using opponent strategy for their moves, random for ours"""
        max_depth = 100

        for _ in range(max_depth):
            if state.is_terminal():
                return state.get_utility(self.bot_player)

            if state.current_player == self.bot_player:
                # Our move - use random for speed (true best response would recurse)
                legal = state.get_legal_actions(apply_pruning=False)
                action = random.choice(legal)
            else:
                # Opponent's move - use their strategy
                action = self.opponent.get_action(state)

            state = apply_action(state, action)

        return state.get_utility(self.bot_player)


def estimate_exploitability(strategy_file: str, num_games: int, rollout_count: int = 30,
                            variant: Optional[str] = None) -> Tuple[float, float]:
    """
    Estimate exploitability of a strategy using approximate best response.

    Exploitability = (BR_value_as_P1 + BR_value_as_P2) / 2

    Where BR_value is how much a best response strategy wins against the target.

    For a Nash equilibrium, exploitability = 0.

    Args:
        strategy_file: Path to strategy JSON
        num_games: Number of games per position
        rollout_count: Rollouts per action for best response
        variant: Force variant

    Returns:
        (exploitability_estimate, standard_error)
    """
    print("\n" + "="*70)
    print("EXPLOITABILITY ESTIMATION")
    print("="*70)

    # Load target strategy
    target_p1 = BotStrategy(strategy_file, bot_player=1, silent=False)
    target_p2 = BotStrategy(strategy_file, bot_player=2, silent=True)

    # Initialize variant
    if variant:
        initialize_variant(variant)
    else:
        initialize_variant(target_p1.variant)

    print(f"Variant: {game_state.CURRENT_VARIANT.get_variant_name()}")
    print(f"Rollouts per action: {rollout_count}")
    print(f"Games per position: {num_games}")

    # Create best response bots
    br_as_p1 = BestResponseBot(target_p2, bot_player=1, rollout_count=rollout_count)
    br_as_p2 = BestResponseBot(target_p1, bot_player=2, rollout_count=rollout_count)

    print(f"\n--- Best Response as Player 1 vs Target ---")
    br_p1_utilities = []
    for i in range(num_games):
        result = play_game(br_as_p1, target_p2, first_player=1)
        if result.winner == 1:
            br_p1_utilities.append(1.0)
        elif result.winner == 2:
            br_p1_utilities.append(-1.0)
        else:
            br_p1_utilities.append(0.0)

        if (i + 1) % (num_games // 10) == 0:
            print(f"  Progress: {i+1}/{num_games}", end="\r")

    br_p1_value = sum(br_p1_utilities) / num_games
    print(f"\nBR as P1 expected value: {br_p1_value:+.4f}")

    print(f"\n--- Best Response as Player 2 vs Target ---")
    br_p2_utilities = []
    for i in range(num_games):
        result = play_game(target_p1, br_as_p2, first_player=1)
        if result.winner == 2:
            br_p2_utilities.append(1.0)
        elif result.winner == 1:
            br_p2_utilities.append(-1.0)
        else:
            br_p2_utilities.append(0.0)

        if (i + 1) % (num_games // 10) == 0:
            print(f"  Progress: {i+1}/{num_games}", end="\r")

    br_p2_value = sum(br_p2_utilities) / num_games
    print(f"\nBR as P2 expected value: {br_p2_value:+.4f}")

    # Exploitability is how much better than 0 the BR can do on average
    # For P1: BR value - game value (if game value is 0, this is just BR value)
    # For P2: BR value - (-game value) = BR value + game value
    # Average exploitability
    exploitability = (max(0, br_p1_value) + max(0, br_p2_value)) / 2

    # Standard error
    all_utilities = br_p1_utilities + br_p2_utilities
    combined_std = math.sqrt(sum((u - sum(all_utilities)/len(all_utilities))**2 for u in all_utilities) / len(all_utilities))
    std_err = combined_std / math.sqrt(len(all_utilities))

    print(f"\n--- Exploitability Estimate ---")
    print(f"Exploitability: {exploitability:.4f} ± {std_err:.4f}")

    if exploitability < 0.05:
        print("Strategy is close to Nash equilibrium (exploitability < 5%)")
    elif exploitability < 0.15:
        print("Strategy has moderate exploitability (5-15%)")
    else:
        print("Strategy is significantly exploitable (>15%)")

    print("="*70)

    return exploitability, std_err


def main():
    parser = argparse.ArgumentParser(
        description="Bot vs Bot Simulation for Coup CFR Strategies",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Compare two strategies
  python simulate_games.py base_strategy_20_1_10000.json base_strategy_18_1_10000.json --games 1000

  # Tournament between multiple strategies
  python simulate_games.py *.json --tournament --games 500

  # Compare with random baseline
  python simulate_games.py my_strategy.json --random --games 1000

  # Verbose output
  python simulate_games.py strat1.json strat2.json --games 100 --verbose

  # Compute game value (self-play)
  python simulate_games.py strategy.json --game-value --games 5000

  # Estimate exploitability
  python simulate_games.py strategy.json --exploitability --games 200
        """
    )

    parser.add_argument("strategies", nargs="+", help="Strategy JSON files to compare")
    parser.add_argument("-g", "--games", type=int, default=100, help="Number of games per matchup (default: 100)")
    parser.add_argument("-t", "--tournament", action="store_true", help="Run round-robin tournament")
    parser.add_argument("-r", "--random", action="store_true", help="Include random baseline bot")
    parser.add_argument("-v", "--verbose", action="store_true", help="Print detailed game-by-game output")
    parser.add_argument("--variant", type=str, choices=["simple", "simpleassassin", "base", "full"],
                        help="Force game variant (auto-detected from filename by default)")
    parser.add_argument("--seed", type=int, help="Random seed for reproducibility")
    parser.add_argument("--game-value", action="store_true", help="Compute game value through self-play")
    parser.add_argument("--exploitability", action="store_true", help="Estimate exploitability via best response")
    parser.add_argument("--rollouts", type=int, default=30, help="Rollouts per action for exploitability (default: 30)")

    args = parser.parse_args()

    # Set random seed if specified
    if args.seed:
        random.seed(args.seed)
        print(f"Random seed: {args.seed}")

    strategies = args.strategies

    # Game value mode (single strategy self-play)
    if args.game_value:
        if len(strategies) != 1:
            print("Error: --game-value requires exactly 1 strategy file")
            sys.exit(1)
        compute_game_value(strategies[0], args.games, args.variant)
        return

    # Exploitability mode (single strategy)
    if args.exploitability:
        if len(strategies) != 1:
            print("Error: --exploitability requires exactly 1 strategy file")
            sys.exit(1)
        estimate_exploitability(strategies[0], args.games, args.rollouts, args.variant)
        return

    # Add random bot if requested
    if args.random:
        strategies = ["RANDOM"] + strategies

    if len(strategies) < 2 and not args.random:
        print("Error: Need at least 2 strategies to compare")
        print("Use --random to compare against a random baseline")
        print("Use --game-value for self-play game value computation")
        sys.exit(1)

    if args.tournament or len(strategies) > 2:
        # Tournament mode
        if "RANDOM" in strategies:
            print("Note: Random bot cannot be used in tournament mode")
            strategies = [s for s in strategies if s != "RANDOM"]

        if len(strategies) < 2:
            print("Error: Need at least 2 strategies for tournament")
            sys.exit(1)

        run_tournament(strategies, args.games, args.variant, args.verbose)
    else:
        # Head-to-head mode
        print("\n" + "="*70)
        print("HEAD-TO-HEAD COMPARISON")
        print("="*70)

        # Load strategies
        print("\nLoading strategies...")

        if strategies[0] == "RANDOM":
            bot1 = RandomBot(bot_player=1)
            print(f"  Bot 1: RandomBot (baseline)")
        else:
            bot1 = BotStrategy(strategies[0], bot_player=1)

        if strategies[1] == "RANDOM":
            bot2 = RandomBot(bot_player=2)
            print(f"  Bot 2: RandomBot (baseline)")
        else:
            bot2 = BotStrategy(strategies[1], bot_player=2)

        # Determine variant
        variant = args.variant
        if not variant:
            if hasattr(bot1, 'variant') and bot1.variant:
                variant = bot1.variant
            elif hasattr(bot2, 'variant') and bot2.variant:
                variant = bot2.variant
            else:
                variant = "base"

        initialize_variant(variant)
        print(f"\nGame variant: {game_state.CURRENT_VARIANT.get_variant_name()}")

        print(f"\nRunning {args.games} games...")

        result = run_match(bot1, bot2, args.games, args.verbose)
        print_match_result(result)


if __name__ == "__main__":
    main()
