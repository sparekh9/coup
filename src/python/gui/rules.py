"""
Variant-specific rules for Coup game.

This module encapsulates the rules for different Coup variants:
- simple: 1 influence, 3 cards (Duke, Captain, Assassin), no blocking
- simpleblocking: 1 influence, 4 cards (+ Contessa), with blocking
- base: 2 influences, 3 cards (Duke, Captain, Assassin), no blocking
"""

from typing import List, Optional, Set
from game_state import GameState, Action, Influence, ChallengeResponse


class CoupRules:
    """Base class for Coup variant rules."""

    # Override these in subclasses
    VARIANT_NAME = "base"
    MAX_INFLUENCES = 2
    STARTING_INFLUENCES = 2
    NUM_INFLUENCE_TYPES = 3
    STARTING_COINS = 2
    COUP_COST = 7
    ASSASSINATE_COST = 3
    MUST_COUP_THRESHOLD = 7
    STEAL_AMOUNT = 2
    TAX_AMOUNT = 3
    INCOME_AMOUNT = 1
    FOREIGN_AID_AMOUNT = 2
    SUPPORTS_BLOCKING = False

    # Available cards and actions for this variant
    AVAILABLE_INFLUENCES: Set[Influence] = {
        Influence.DUKE,
        Influence.CAPTAIN,
        Influence.ASSASSIN
    }

    AVAILABLE_ACTIONS: Set[Action] = {
        Action.INCOME,
        Action.FOREIGN_AID,
        Action.TAX,
        Action.STEAL,
        Action.ASSASSINATE,
        Action.COUP
    }

    @classmethod
    def get_required_influence(cls, action: Action) -> Optional[Influence]:
        """Get the influence card required to perform an action."""
        mapping = {
            Action.TAX: Influence.DUKE,
            Action.STEAL: Influence.CAPTAIN,
            Action.ASSASSINATE: Influence.ASSASSIN,
        }
        return mapping.get(action)

    @classmethod
    def get_action_cost(cls, action: Action) -> int:
        """Get coin cost of an action."""
        if action == Action.COUP:
            return cls.COUP_COST
        elif action == Action.ASSASSINATE:
            return cls.ASSASSINATE_COST
        return 0

    @classmethod
    def is_challengeable(cls, action: Action) -> bool:
        """Check if an action can be challenged."""
        return action in {Action.TAX, Action.STEAL, Action.ASSASSINATE}

    @classmethod
    def is_blockable(cls, action: Action) -> bool:
        """Check if an action can be blocked."""
        return False  # Override in blocking variants

    @classmethod
    def get_blocking_influence(cls, action: Action) -> Optional[Influence]:
        """Get the influence that can block this action."""
        return None  # Override in blocking variants

    @classmethod
    def get_legal_actions(cls, state: GameState) -> List[Action]:
        """Get list of legal actions for current player."""
        coins = state.get_player_coins(state.current_player)
        actions = []

        # Force COUP at threshold
        if coins >= cls.MUST_COUP_THRESHOLD:
            return [Action.COUP]

        # Always available
        actions.append(Action.INCOME)
        actions.append(Action.FOREIGN_AID)
        actions.append(Action.TAX)
        actions.append(Action.STEAL)

        # Conditional on coins
        if coins >= cls.ASSASSINATE_COST:
            actions.append(Action.ASSASSINATE)
        if coins >= cls.COUP_COST:
            actions.append(Action.COUP)

        return actions

    @classmethod
    def should_force_challenge(cls, state: GameState, pending_action: Action) -> bool:
        """
        Determine if challenger MUST challenge (no PASS option).

        This implements perfect information challenge forcing:
        - Force challenge if all required cards are revealed
        - Force challenge if defender holds all copies
        """
        required = cls.get_required_influence(pending_action)
        if not required:
            return False

        my_influences = state.get_player_influences(state.current_player)

        # Count revealed copies
        revealed_count = sum(1 for card in state.revealed_cards if card == required)

        # Count my copies
        my_count = sum(1 for card in my_influences if card == required)

        # Rule 1: One influence + Assassination → only CHALLENGE
        # (But NOT for blocking variants - Contessa can block, so this rule doesn't apply)
        if not cls.SUPPORTS_BLOCKING and pending_action == Action.ASSASSINATE and len(my_influences) == 1:
            return True

        # Rule 2: All required cards revealed → only CHALLENGE
        # (2 of each in simple/base variants)
        if revealed_count >= 2:
            return True

        # Rule 3 (for 2-influence variants): I hold both copies → only CHALLENGE
        if cls.MAX_INFLUENCES == 2 and my_count >= 2:
            return True

        # Rule 4 (for 2-influence variants): One held + one revealed → only CHALLENGE
        if cls.MAX_INFLUENCES == 2 and (my_count + revealed_count >= 2):
            return True

        # Rule for 1-influence variants: I hold the card and one revealed → CHALLENGE
        if cls.MAX_INFLUENCES == 1 and my_count > 0 and revealed_count >= 1:
            return True

        return False


    @classmethod
    def should_force_challenge_block(cls, state: GameState, claimed_blocker: Influence) -> bool:
        """Determine if player MUST challenge a block claim."""
        # Use similar logic to should_force_challenge
        my_influences = state.get_player_influences(state.current_player)

        revealed_count = sum(1 for card in state.revealed_cards if card == claimed_blocker)
        my_count = sum(1 for card in my_influences if card == claimed_blocker)

        # Force challenge if all cards accounted for
        if cls.MAX_INFLUENCES == 1:
            if revealed_count >= 2:
                return True
            if my_count > 0 and revealed_count >= 1:
                return True
        else:  # MAX_INFLUENCES == 2
            if revealed_count >= 2:
                return True
            if my_count >= 2:
                return True
            if my_count + revealed_count >= 2:
                return True

        return False

    @classmethod
    def should_force_block(cls, state: GameState, action: Action) -> bool:
        """
        Determine if player MUST block (no PASS/CHALLENGE options).

        Force block if we have the blocking card - this is always the dominant strategy.
        - Contessa should always block Assassinate
        - Captain should always block Steal
        """
        if not cls.is_blockable(action):
            return False

        blocking_card = cls.get_blocking_influence(action)
        if not blocking_card:
            return False

        my_influences = state.get_player_influences(state.current_player)

        # Check if I have the blocking card
        return blocking_card in my_influences


class SimpleCoupRules(CoupRules):
    """Rules for Simple variant: 1 influence, 3 cards, no blocking."""

    VARIANT_NAME = "simple"
    MAX_INFLUENCES = 1
    STARTING_INFLUENCES = 1
    NUM_INFLUENCE_TYPES = 3
    MUST_COUP_THRESHOLD = 7
    SUPPORTS_BLOCKING = False


class SimpleCoupBlockingRules(SimpleCoupRules):
    """Rules for Simple Blocking variant: 1 influence, 4 cards, with blocking."""

    VARIANT_NAME = "simpleblocking"
    NUM_INFLUENCE_TYPES = 4
    SUPPORTS_BLOCKING = True

    AVAILABLE_INFLUENCES = SimpleCoupRules.AVAILABLE_INFLUENCES | {Influence.CONTESSA}

    @classmethod
    def is_blockable(cls, action: Action) -> bool:
        """Steal and Assassinate can be blocked."""
        return action in {Action.STEAL, Action.ASSASSINATE}

    @classmethod
    def get_blocking_influence(cls, action: Action) -> Optional[Influence]:
        """Get the influence that can block this action."""
        if action == Action.STEAL:
            return Influence.CAPTAIN
        elif action == Action.ASSASSINATE:
            return Influence.CONTESSA
        return None


class BaseCoupRules(CoupRules):
    """Rules for Base variant: 2 influences, 3 cards, no blocking."""

    VARIANT_NAME = "base"
    MAX_INFLUENCES = 2
    STARTING_INFLUENCES = 2
    NUM_INFLUENCE_TYPES = 3
    MUST_COUP_THRESHOLD = 7
    SUPPORTS_BLOCKING = False


# Variant registry
VARIANT_RULES = {
    "simple": SimpleCoupRules,
    "simpleblocking": SimpleCoupBlockingRules,
    "base": BaseCoupRules,
}


def get_rules(variant: str) -> type[CoupRules]:
    """Get rules class for a variant."""
    if variant not in VARIANT_RULES:
        raise ValueError(f"Unknown variant: {variant}. Available: {list(VARIANT_RULES.keys())}")
    return VARIANT_RULES[variant]
