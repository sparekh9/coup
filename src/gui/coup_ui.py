#!/usr/bin/env python3
"""
Coup Game UI - Play against a CFR-trained bot
"""

import json
import random
import re
import sys
from pathlib import Path
from typing import Dict, List, Tuple

# Add parent directory to path to import game_state
sys.path.insert(0, str(Path(__file__).parent.parent / 'python'))

from game_state import (
    SimpleGameState, ChallengeResponse,
    apply_action, create_initial_state, ABSTRACTION_MODE,
    initialize_variant, CURRENT_VARIANT
)


class BotStrategy:
    """Loads and uses CFR strategy from JSON file"""

    def __init__(self, strategy_file: str):
        self.strategy_file = strategy_file
        self.strategy: Dict[str, Dict[str, float]] = {}
        self.variant = "base"  # default
        self.load_strategy()
        self.detect_variant()

    def load_strategy(self):
        """Load strategy from JSON file"""
        try:
            with open(self.strategy_file, 'r') as f:
                self.strategy = json.load(f)
            print(f"✓ Loaded strategy with {len(self.strategy)} information sets")
        except FileNotFoundError:
            print(f"Error: Strategy file '{self.strategy_file}' not found!")
            sys.exit(1)
        except json.JSONDecodeError as e:
            print(f"Error: Invalid JSON in strategy file: {e}")
            sys.exit(1)

    def detect_variant(self):
        """Detect game variant from filename"""
        filename = Path(self.strategy_file).stem.lower()

        # Check for simpleassassin before simple (since simpleassassin contains "simple")
        if 'simpleassassin' in filename:
            self.variant = "simpleassassin"
            print(f"✓ Detected variant: Simple Assassin Coup (1 influence, WITH assassinate)")
        elif 'base' in filename:
            self.variant = "base"
            print(f"✓ Detected variant: Base Coup (2 influences, 3 card types)")
        elif 'simple' in filename:
            self.variant = "simple"
            print(f"✓ Detected variant: Simple Coup (1 influence, no assassinate)")
        elif 'full' in filename:
            self.variant = "full"
            print(f"✓ Detected variant: Full Coup (5 card types)")
        else:
            print(f"⚠ Warning: Could not detect variant from filename, using 'base'")
            self.variant = "base"

    def get_action(self, state: SimpleGameState):
        """Get bot's action based on loaded strategy"""
        # Get info set key (bot is always player 2)
        info_set_key = state.get_info_set_key(2)

        # Look up strategy
        if info_set_key not in self.strategy:
            # Strategy not found, use random legal action
            print(f"⚠ Warning: Info set {info_set_key} not in strategy, using random action")
            legal_actions = state.get_legal_actions(apply_pruning=False)
            return random.choice(legal_actions)

        action_probs = self.strategy[info_set_key]
        print("Found strategy for info set", info_set_key)
        print("Strategy:", action_probs)

        # Convert action names to action objects
        legal_actions = state.get_legal_actions(apply_pruning=False)
        action_dict = {}

        for action in legal_actions:
            action_name = action_to_string(action)
            if action_name in action_probs:
                action_dict[action] = action_probs[action_name]

        if not action_dict:
            # No valid actions found, use random
            print(f"⚠ Warning: No valid actions in strategy for {info_set_key}")
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


def action_to_string(action) -> str:
    """Convert action to string matching C++ format"""
    if isinstance(action, ChallengeResponse):
        return "PASS" if action == ChallengeResponse.PASS else "CHALLENGE"
    else:
        return action.name


def influence_to_string(influence) -> str:
    """Convert influence to display string"""
    return influence.name


def display_state(state: SimpleGameState, hide_bot_cards: bool = True):
    """Display current game state"""
    print("\n" + "="*60)
    print("GAME STATE")
    print("="*60)

    # Player 1 (Human)
    print("\n👤 YOU (Player 1):")
    print(f"  Coins: {state.p1_coins}")
    print(f"  Influences: {', '.join(influence_to_string(i) for i in state.p1_influences)}")

    # Player 2 (Bot)
    print("\n🤖 BOT (Player 2):")
    print(f"  Coins: {state.p2_coins}")
    if hide_bot_cards:
        print(f"  Influences: {len(state.p2_influences)} card(s) remaining")
    else:
        print(f"  Influences: {', '.join(influence_to_string(i) for i in state.p2_influences)}")

    # Revealed cards
    if state.revealed_cards:
        print(f"\n📋 Revealed cards: {', '.join(influence_to_string(i) for i in state.revealed_cards)}")

    # Deck
    print(f"📚 Cards remaining in deck: {len(state.deck)}")

    # Current turn
    current = "YOU" if state.current_player == 1 else "BOT"
    print(f"\n🎯 Current turn: {current}")

    # Pending action
    if state.pending_action:
        actor, action = state.pending_action
        actor_name = "YOU" if actor == 1 else "BOT"
        print(f"⏳ Pending: {actor_name} used {action.name}")

    print("="*60)


def get_human_action(state: SimpleGameState):
    """Get action from human player"""
    legal_actions = state.get_legal_actions(apply_pruning=False)
    Action = CURRENT_VARIANT.Action

    print("\n🎮 Your available actions:")
    for i, action in enumerate(legal_actions, 1):
        action_str = action_to_string(action)

        # Add helpful descriptions
        desc = ""
        if not isinstance(action, ChallengeResponse):
            if action == Action.INCOME:
                desc = f" (+{CURRENT_VARIANT.INCOME_AMOUNT} coin)"
            elif action == Action.TAX:
                desc = f" (+{CURRENT_VARIANT.TAX_AMOUNT} coins, claims Duke)"
            elif action == Action.STEAL:
                desc = f" (+{CURRENT_VARIANT.STEAL_AMOUNT} coins from opponent, claims Captain)"
            elif hasattr(Action, 'ASSASSINATE') and action == Action.ASSASSINATE:
                desc = f" (-{CURRENT_VARIANT.ASSASSINATE_COST} coins, opponent loses influence, claims Assassin)"
            elif action == Action.COUP:
                desc = f" (-{CURRENT_VARIANT.COUP_COST} coins, opponent loses influence)"
            elif hasattr(Action, 'FOREIGN_AID') and action == Action.FOREIGN_AID:
                desc = f" (+{CURRENT_VARIANT.FOREIGN_AID_AMOUNT} coins)"
            elif hasattr(Action, 'EXCHANGE') and action == Action.EXCHANGE:
                desc = " (draw and exchange cards, claims Ambassador)"
        else:
            if action == ChallengeResponse.PASS:
                desc = " (allow action)"
            elif action == ChallengeResponse.CHALLENGE:
                desc = " (challenge claim)"

        print(f"  {i}. {action_str}{desc}")

    while True:
        try:
            choice = input("\nEnter action number: ").strip()
            idx = int(choice) - 1
            if 0 <= idx < len(legal_actions):
                return legal_actions[idx]
            else:
                print(f"Invalid choice. Please enter 1-{len(legal_actions)}")
        except (ValueError, KeyboardInterrupt):
            print("\nInvalid input. Please enter a number.")


def display_action(player: int, action, state: SimpleGameState):
    """Display an action taken by a player"""
    player_name = "YOU" if player == 1 else "BOT"
    action_str = action_to_string(action)
    Action = CURRENT_VARIANT.Action

    # Special formatting for different actions
    if isinstance(action, ChallengeResponse):
        if action == ChallengeResponse.CHALLENGE:
            print(f"\n⚔️  {player_name} CHALLENGES!")
        else:
            print(f"\n✅ {player_name} allows the action")
    else:
        # Determine emoji based on action type
        money_actions = [Action.INCOME, Action.TAX, Action.STEAL]
        if hasattr(Action, 'FOREIGN_AID'):
            money_actions.append(Action.FOREIGN_AID)

        emoji = "💰" if action in money_actions else "⚔️"
        print(f"\n{emoji} {player_name} uses {action_str}")

        # Show what was claimed
        claim = None
        if action == Action.TAX:
            claim = "DUKE"
        elif action == Action.STEAL:
            claim = "CAPTAIN"
        elif hasattr(Action, 'ASSASSINATE') and action == Action.ASSASSINATE:
            claim = "ASSASSIN"
        elif hasattr(Action, 'EXCHANGE') and action == Action.EXCHANGE:
            claim = "AMBASSADOR"

        if claim:
            print(f"   (Claims to have {claim})")


def play_game(strategy_file: str):
    """Main game loop"""
    print("\n" + "="*60)
    print("🎲 COUP - Play Against CFR Bot")
    print("="*60)

    # Load bot strategy
    bot = BotStrategy(strategy_file)

    # Initialize the variant based on detected variant
    initialize_variant(bot.variant)
    print(f"✓ Initialized game with {CURRENT_VARIANT.get_variant_name()} rules")

    # Show abstraction mode warning
    if ABSTRACTION_MODE.value != 0:
        print(f"\n⚠️  WARNING: Python abstraction mode is {ABSTRACTION_MODE.name}")
        print(f"   Make sure this matches your C++ training configuration!")

    # Create initial state
    state = create_initial_state()

    print(f"\n🎴 Initial cards dealt!")
    print(f"   Your influences: {', '.join(influence_to_string(i) for i in state.p1_influences)}")

    input("\nPress Enter to start the game...")

    # Game loop
    turn = 0
    while not state.is_terminal():
        turn += 1
        print(f"\n{'='*60}")
        print(f"TURN {turn}")
        print(f"{'='*60}")

        display_state(state)

        # Get action from current player
        if state.current_player == 1:
            # Human's turn
            action = get_human_action(state)
            display_action(1, action, state)
        else:
            # Bot's turn
            print("\n🤖 Bot is thinking...")
            action = bot.get_action(state)
            display_action(2, action, state)

        # Apply action
        state = apply_action(state, action)

        # Check if game ended
        if state.is_terminal():
            break

        # Short pause for readability
        if state.current_player == 2:  # After human action
            input("\nPress Enter to continue...")

    # Game over - show results
    print("\n" + "="*60)
    print("🎊 GAME OVER!")
    print("="*60)

    display_state(state, hide_bot_cards=False)

    # Determine winner
    utility = state.get_utility(1)
    if utility > 0:
        print("\n🎉 YOU WIN! 🎉")
    elif utility < 0:
        print("\n🤖 BOT WINS! Better luck next time!")
    else:
        print("\n🤝 IT'S A DRAW!")

    print(f"\nGame lasted {turn} turns")
    print(f"Final score: You {len(state.p1_influences)} influences, Bot {len(state.p2_influences)} influences")


def main():
    """Entry point"""
    if len(sys.argv) < 2:
        print("Usage: python coup_ui.py <strategy_file.json>")
        print("\nExample:")
        print("  python coup_ui.py base_strategy_14_10_10000.json")
        sys.exit(1)

    strategy_file = sys.argv[1]
    play_game(strategy_file)


if __name__ == "__main__":
    main()
