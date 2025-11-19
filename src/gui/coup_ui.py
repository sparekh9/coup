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

    def __init__(self, strategy_file: str, bot_player: int = 2):
        self.strategy_file = strategy_file
        self.strategy: Dict[str, Dict[str, float]] = {}
        self.variant = "base"  # default
        self.bot_player = bot_player
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
        # Get info set key for bot player
        info_set_key = state.get_info_set_key(self.bot_player)

        # Look up strategy
        if info_set_key not in self.strategy:
            # Strategy not found, use random legal action
            print(f"\n⚠ WARNING: Info set {info_set_key} not in strategy!")
            print(f"  Bot is Player {self.bot_player}, current_player={state.current_player}")
            print(f"  This means the strategy file may not have been trained for this player perspective")
            print(f"  Using RANDOM action instead")
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


def display_state(state: SimpleGameState, human_player: int = 1, hide_bot_cards: bool = True):
    """Display current game state"""
    print("\n" + "="*60)
    print("GAME STATE")
    print("="*60)

    bot_player = 3 - human_player  # If human is 1, bot is 2; if human is 2, bot is 1

    # Get player states
    human_coins = state.p1_coins if human_player == 1 else state.p2_coins
    human_influences = state.p1_influences if human_player == 1 else state.p2_influences
    bot_coins = state.p2_coins if human_player == 1 else state.p1_coins
    bot_influences = state.p2_influences if human_player == 1 else state.p1_influences

    # Display human player
    print(f"\n👤 YOU (Player {human_player}):")
    print(f"  Coins: {human_coins}")
    print(f"  Influences: {', '.join(influence_to_string(i) for i in human_influences)}")

    # Display bot player
    print(f"\n🤖 BOT (Player {bot_player}):")
    print(f"  Coins: {bot_coins}")
    if hide_bot_cards:
        print(f"  Influences: {len(bot_influences)} card(s) remaining")
    else:
        print(f"  Influences: {', '.join(influence_to_string(i) for i in bot_influences)}")

    # Revealed cards
    if state.revealed_cards:
        print(f"\n📋 Revealed cards: {', '.join(influence_to_string(i) for i in state.revealed_cards)}")

    # Deck
    print(f"📚 Cards remaining in deck: {len(state.deck)}")

    # Current turn
    current = "YOU" if state.current_player == human_player else "BOT"
    print(f"\n🎯 Current turn: {current}")

    # Pending action
    if state.pending_action:
        actor, action = state.pending_action
        actor_name = "YOU" if actor == human_player else "BOT"
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


def display_action(player: int, action, state: SimpleGameState, human_player: int = 1):
    """Display an action taken by a player"""
    player_name = "YOU" if player == human_player else "BOT"
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


def play_game(strategy_file: str, human_player: int = 1):
    """Main game loop"""
    print("\n" + "="*60)
    print("🎲 COUP - Play Against CFR Bot")
    print("="*60)

    bot_player = 3 - human_player  # If human is 1, bot is 2; if human is 2, bot is 1
    print(f"\nPlayer assignment: You are Player {human_player}, Bot is Player {bot_player}")

    # Load bot strategy
    bot = BotStrategy(strategy_file, bot_player)

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
    human_influences = state.p1_influences if human_player == 1 else state.p2_influences
    print(f"   Your influences: {', '.join(influence_to_string(i) for i in human_influences)}")
    print(f"   You are Player {human_player}")

    input("\nPress Enter to start the game...")

    # Game loop
    turn = 0
    while not state.is_terminal():
        turn += 1
        print(f"\n{'='*60}")
        print(f"TURN {turn}")
        print(f"{'='*60}")

        display_state(state, human_player)

        # Get action from current player
        if state.current_player == human_player:
            # Human's turn
            action = get_human_action(state)
            display_action(state.current_player, action, state, human_player)
        else:
            # Bot's turn
            print("\n🤖 Bot is thinking...")
            action = bot.get_action(state)
            display_action(state.current_player, action, state, human_player)

        # Apply action
        state = apply_action(state, action)

        # Check if game ended
        if state.is_terminal():
            break

        # Short pause for readability
        if state.current_player == bot_player:  # After human action
            input("\nPress Enter to continue...")

    # Game over - show results
    print("\n" + "="*60)
    print("🎊 GAME OVER!")
    print("="*60)

    display_state(state, human_player, hide_bot_cards=False)

    # Determine winner from human player's perspective
    utility = state.get_utility(human_player)
    if utility > 0:
        print("\n🎉 YOU WIN! 🎉")
    elif utility < 0:
        print("\n🤖 BOT WINS! Better luck next time!")
    else:
        print("\n🤝 IT'S A DRAW!")

    human_influences = state.p1_influences if human_player == 1 else state.p2_influences
    bot_influences = state.p2_influences if human_player == 1 else state.p1_influences
    print(f"\nGame lasted {turn} turns")
    print(f"Final score: You {len(human_influences)} influences, Bot {len(bot_influences)} influences")


def main():
    """Entry point"""
    if len(sys.argv) < 2:
        print("Usage: python coup_ui.py <strategy_file.json> [player]")
        print("\nArguments:")
        print("  strategy_file.json - Path to the CFR strategy file")
        print("  player - Which player you want to be: 1 or 2 (default: 1)")
        print("\nExamples:")
        print("  python coup_ui.py base_strategy_14_10_10000.json")
        print("  python coup_ui.py base_strategy_14_10_10000.json 2")
        sys.exit(1)

    strategy_file = sys.argv[1]

    # Get player selection
    human_player = 1  # default
    if len(sys.argv) >= 3:
        try:
            human_player = int(sys.argv[2])
            if human_player not in [1, 2]:
                print("Error: Player must be 1 or 2")
                sys.exit(1)
        except ValueError:
            print("Error: Player must be a number (1 or 2)")
            sys.exit(1)

    play_game(strategy_file, human_player)


if __name__ == "__main__":
    main()
