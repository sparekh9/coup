"""
Game state representation for Coup variants.

This module provides a Python implementation of the game state,
mirroring the C++ GameState structure but in a more Pythonic way.
"""

from dataclasses import dataclass, field
from typing import List, Optional, Tuple
from enum import Enum


class Influence(Enum):
    """Influence cards - varies by variant."""
    DUKE = 0
    CAPTAIN = 1
    ASSASSIN = 2
    CONTESSA = 3  # Only in blocking variants


class Action(Enum):
    """Player actions - varies by variant."""
    INCOME = 0
    FOREIGN_AID = 1
    TAX = 2
    STEAL = 3
    ASSASSINATE = 4
    COUP = 5


class ChallengeResponse(Enum):
    """Response to a challengeable action (now also handles blocking)."""
    PASS = 0
    CHALLENGE = 1
    BLOCK = 2  # Block the action (optimization: merged with challenge response)


@dataclass
class GameState:
    """
    Represents the current state of a Coup game.

    This mirrors the C++ GameState but uses Python conventions.
    Supports multiple variants: simple, simpleblocking, base.
    """

    # Variant configuration
    variant: str  # "simple", "simpleblocking", "base"
    max_influences: int  # 1 for simple, 2 for base

    # Player influences (list of Influence enums)
    p1_influences: List[Influence] = field(default_factory=list)
    p2_influences: List[Influence] = field(default_factory=list)

    # Coins
    p1_coins: int = 2
    p2_coins: int = 2

    # Current player (1 or 2)
    current_player: int = 1

    # Revealed cards (lost influences)
    revealed_cards: List[Influence] = field(default_factory=list)

    # Pending action state
    has_pending_action: bool = False
    pending_action_actor: int = 0
    pending_action_type: Optional[Action] = None

    # Pending block challenge state (when someone blocks and gets challenged)
    has_pending_block_challenge: bool = False
    pending_block_challenger: int = 0  # Who initiated the original action
    pending_block_claim: Optional[Influence] = None  # What card the blocker claimed
    pending_block_action: Optional[Action] = None  # Needed for execute_action if block fails

    # Action history (for info set calculation)
    action_history: List[str] = field(default_factory=list)

    # Claim history (pairs of player_id, claimed_influence)
    claim_history: List[Tuple[int, Influence]] = field(default_factory=list)

    def __post_init__(self):
        """Validate state after initialization."""
        if self.variant not in ["simple", "simpleblocking", "base"]:
            raise ValueError(f"Unknown variant: {self.variant}")

        if self.variant == "simple":
            self.max_influences = 1
        elif self.variant in ["simpleblocking", "base"]:
            self.max_influences = 2

    def get_player_influences(self, player_id: int) -> List[Influence]:
        """Get influences for specified player."""
        return self.p1_influences if player_id == 1 else self.p2_influences

    def get_player_coins(self, player_id: int) -> int:
        """Get coins for specified player."""
        return self.p1_coins if player_id == 1 else self.p2_coins

    def set_player_coins(self, player_id: int, coins: int):
        """Set coins for specified player."""
        if player_id == 1:
            self.p1_coins = coins
        else:
            self.p2_coins = coins

    def get_influence_count(self, player_id: int) -> int:
        """Get number of active influences for player."""
        influences = self.get_player_influences(player_id)
        return len(influences)

    def is_terminal(self) -> bool:
        """Check if game is over (one player has no influences)."""
        return len(self.p1_influences) == 0 or len(self.p2_influences) == 0

    def get_winner(self) -> Optional[int]:
        """Get winner (1 or 2) if game is over, else None."""
        if len(self.p1_influences) == 0:
            return 2
        elif len(self.p2_influences) == 0:
            return 1
        return None

    def get_state_description(self) -> str:
        """Get human-readable state description."""
        desc = f"=== {self.variant.upper()} Coup Game ===\n"
        desc += f"\nPlayer 1: {self.p1_coins} coins, {len(self.p1_influences)} influence(s)\n"
        desc += f"Player 2: {self.p2_coins} coins, {len(self.p2_influences)} influence(s)\n"
        desc += f"\nCurrent player: {self.current_player}\n"

        if self.has_pending_action:
            desc += f"\nPending action: Player {self.pending_action_actor} played {self.pending_action_type.name}\n"
        elif self.has_pending_block_challenge:
            desc += f"\nPending block challenge: Blocking with {self.pending_block_claim.name}\n"

        if self.revealed_cards:
            desc += f"\nRevealed cards: {[c.name for c in self.revealed_cards]}\n"

        return desc

    def clone(self) -> 'GameState':
        """Create a deep copy of this state."""
        import copy
        return copy.deepcopy(self)
