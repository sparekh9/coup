#!/usr/bin/env python3
"""
Coup Game Demo - Play against a random bot (no training required)
Use this to test the game UI without needing a trained strategy file.
"""

import random
import sys
from pathlib import Path

# Add parent directory to path to import game_state
sys.path.insert(0, str(Path(__file__).parent.parent / 'python'))

from game_state import (
    SimpleGameState, ChallengeResponse,
    apply_action, create_initial_state, ABSTRACTION_MODE,
    initialize_variant, CURRENT_VARIANT
)


class RandomBot:
    """Simple random bot for testing - no strategy file needed"""

    def __init__(self, variant: str = "base"):
        self.variant = variant

    def get_action(self, state: SimpleGameState):
        """Get random action from legal actions"""
        legal_actions = state.get_legal_actions(apply_pruning=False)
        return random.choice(legal_actions)


def action_to_string(action) -> str:
    """Convert action to string"""
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


def play_demo(variant: str = "base", human_player: int = 1):
    """Main game loop with random bot"""
    print("\n" + "="*60)
    print("🎲 COUP - Demo Mode (Random Bot)")
    print("="*60)
    print("\nThis demo uses a random bot - no training required!")
    print("Use this to learn the game or test the UI.")
    print("\nFor a real challenge, train a bot with CFR and use coup_ui.py")

    bot_player = 3 - human_player  # If human is 1, bot is 2; if human is 2, bot is 1

    # Initialize the variant
    initialize_variant(variant)
    print(f"\n✓ Playing {CURRENT_VARIANT.get_variant_name()}")
    print(f"  - {CURRENT_VARIANT.STARTING_INFLUENCES} influence(s) per player")
    print(f"  - {CURRENT_VARIANT.NUM_INFLUENCE_TYPES} card types")

    # Create random bot
    bot = RandomBot(variant)

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
            print("\n🤖 Bot is thinking (randomly)...")
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
        print("\nReady for a real challenge? Train a CFR bot:")
        print("  ./coup_train --variant base --iterations 50000")
        print("  python src/gui/coup_ui.py base_strategy_14_10_50000.json")
    elif utility < 0:
        print("\n🤖 BOT WINS! (even though it was playing randomly!)")
        print("\nImagine how tough a trained bot would be...")
    else:
        print("\n🤝 IT'S A DRAW!")

    human_influences = state.p1_influences if human_player == 1 else state.p2_influences
    bot_influences = state.p2_influences if human_player == 1 else state.p1_influences
    print(f"\nGame lasted {turn} turns")
    print(f"Final score: You {len(human_influences)} influences, Bot {len(bot_influences)} influences")


def main():
    """Entry point"""
    # Parse command-line arguments
    variant = "base"  # default
    human_player = 1  # default

    if len(sys.argv) > 1:
        variant = sys.argv[1].lower()
        if variant not in ["simple", "simpleassassin", "base", "full"]:
            print(f"Error: Unknown variant '{variant}'")
            print("Usage: python coup_demo.py [variant] [player]")
            print("  variant: simple, simpleassassin, base, or full (default: base)")
            print("  player: Which player you want to be: 1 or 2 (default: 1)")
            sys.exit(1)

    if len(sys.argv) > 2:
        try:
            human_player = int(sys.argv[2])
            if human_player not in [1, 2]:
                print("Error: Player must be 1 or 2")
                sys.exit(1)
        except ValueError:
            print("Error: Player must be a number (1 or 2)")
            sys.exit(1)

    play_demo(variant, human_player)


if __name__ == "__main__":
    main()
