"""
Bot player that uses CFR strategies from JSON files.

This module loads trained strategies and computes info set keys
matching the C++ implementation.
"""

import json
import random
from typing import Dict, List, Tuple, Union
from game_state import GameState, Action, Influence, ChallengeResponse
from rules import get_rules


class BotPlayer:
    """
    Bot player that uses a trained CFR strategy.

    Loads strategy from JSON file and makes decisions by:
    1. Computing the info set key (matching C++ implementation)
    2. Looking up the strategy for that info set
    3. Sampling an action according to the probability distribution
    """

    def __init__(self, strategy_file: str, variant: str, player_id: int):
        """
        Initialize bot player.

        Args:
            strategy_file: Path to JSON strategy file
            variant: Game variant ("simple", "simpleblocking", "base")
            player_id: Player number (1 or 2)
        """
        self.variant = variant
        self.player_id = player_id
        self.rules = get_rules(variant)

        # Load strategy from JSON
        with open(strategy_file, 'r') as f:
            self.strategy = json.load(f)

        print(f"Loaded strategy with {len(self.strategy)} info sets")

    def get_action(self, state: GameState) -> Union[Action, ChallengeResponse]:
        """
        Get action for current state using the trained strategy.

        Args:
            state: Current game state

        Returns:
            Action to take (sampled from strategy distribution)
        """
        # Compute info set key
        info_set_key = self._compute_info_set_key(state)
        info_set_hex = f"0x{info_set_key:x}"

        # Look up strategy
        if info_set_hex not in self.strategy:
            # If info set not in strategy, use uniform random
            print(f"Warning: Info set {info_set_hex} not found in strategy, using uniform random")
            return self._get_uniform_action(state)

        action_probs = self.strategy[info_set_hex]

        # Sample action according to probabilities
        actions = list(action_probs.keys())
        probs = list(action_probs.values())

        # Normalize probabilities (in case of small numerical errors)
        total = sum(probs)
        if total > 0:
            probs = [p / total for p in probs]
        else:
            probs = [1.0 / len(probs)] * len(probs)

        chosen_action_str = random.choices(actions, weights=probs)[0]

        # Convert string to appropriate enum
        return self._parse_action(chosen_action_str)

    def _get_uniform_action(self, state: GameState) -> Union[Action, ChallengeResponse]:
        """Get uniform random action when info set not found."""
        if state.has_pending_block_challenge or state.has_pending_action:
            # Could be PASS, CHALLENGE, or BLOCK depending on what's legal
            return random.choice([ChallengeResponse.PASS, ChallengeResponse.CHALLENGE, ChallengeResponse.BLOCK])
        else:
            legal_actions = self.rules.get_legal_actions(state)
            return random.choice(legal_actions)

    def _parse_action(self, action_str: str) -> Union[Action, ChallengeResponse]:
        """Parse action string to appropriate enum."""
        # Try ChallengeResponse (including BLOCK)
        if action_str in ["PASS", "CHALLENGE", "BLOCK"]:
            return ChallengeResponse[action_str]

        # Try Action
        return Action[action_str]

    def _compute_info_set_key(self, state: GameState) -> int:
        """
        Compute info set key matching the C++ implementation.

        This exactly replicates the bit-packing scheme from game_state.tpp.
        """
        # Get player-specific information
        if self.player_id == 1:
            my_influences = state.p1_influences[:]
            my_coins = state.p1_coins
            opp_inf_count = len(state.p2_influences)
            opp_coins = state.p2_coins
            my_claims = self._get_claim_history(state, 1)
            opp_claims = self._get_claim_history(state, 2)
        else:
            my_influences = state.p2_influences[:]
            my_coins = state.p2_coins
            opp_inf_count = len(state.p1_influences)
            opp_coins = state.p1_coins
            my_claims = self._get_claim_history(state, 2)
            opp_claims = self._get_claim_history(state, 1)

        my_inf_count = len(my_influences)

        # Sort influences for canonical representation
        my_influences_sorted = sorted([inf.value for inf in my_influences])

        # Sort revealed cards
        revealed_sorted = sorted([card.value for card in state.revealed_cards])

        # Build hash using FIXED-WIDTH bit packing (52 bits total after optimization)
        hash_val = 1

        # Pack my influences (ALWAYS 2 slots of 3 bits)
        for i in range(2):
            if i < self.rules.MAX_INFLUENCES and i < my_inf_count:
                hash_val = (hash_val << 3) | my_influences_sorted[i]
            else:
                hash_val = (hash_val << 3) | 7  # Empty slot

        # Pack coins (ALWAYS 4 bits for both players)
        # With ABSTRACTION_MODE = NONE, use exact coins
        my_coins_hash = my_coins
        opp_coins_hash = opp_coins
        hash_val = (hash_val << 4) | my_coins_hash
        hash_val = (hash_val << 4) | opp_coins_hash

        # Pack opponent influence count (2 bits)
        hash_val = (hash_val << 2) | opp_inf_count

        # Pack revealed cards (ALWAYS 4 slots of 3 bits)
        for i in range(4):
            if i < len(revealed_sorted):
                hash_val = (hash_val << 3) | revealed_sorted[i]
            else:
                hash_val = (hash_val << 3) | 7  # Empty slot

        # Pack pending state (4 bits: 1 bit state type + 3 bits action/claim)
        # Optimization: merged block and challenge decision, so no has_pending_block state
        if state.has_pending_block_challenge:
            pending_state_bits = 1  # 1
            pending_state_bits = (pending_state_bits << 3) | state.pending_block_claim.value
        elif state.has_pending_action:
            pending_state_bits = 0  # 0
            pending_state_bits = (pending_state_bits << 3) | state.pending_action_type.value
        else:
            pending_state_bits = 0  # 0
            pending_state_bits = (pending_state_bits << 3) | 7
        hash_val = (hash_val << 4) | pending_state_bits

        # Pack my claim history (3 slots of 3 bits each)
        for i in range(3):
            hash_val = (hash_val << 3) | my_claims[i]

        # Pack opponent claim history (3 slots of 3 bits each)
        for i in range(3):
            hash_val = (hash_val << 3) | opp_claims[i]

        # Pack my claim count (2 bits)
        my_claim_cnt = min(len([c for c in my_claims if c != 7]), 3)
        hash_val = (hash_val << 2) | my_claim_cnt

        # Pack opponent claim count (2 bits)
        opp_claim_cnt = min(len([c for c in opp_claims if c != 7]), 3)
        hash_val = (hash_val << 2) | opp_claim_cnt

        return hash_val

    def _get_claim_history(self, state: GameState, player_id: int) -> List[int]:
        """
        Extract claim history for a player from state.claim_history.

        Returns array of 3 values (last 3 claims), with 7 for empty slots.
        """
        claims = [7, 7, 7]  # Initialize with empty
        claim_count = 0

        # Extract claims for this player from state.claim_history
        for pid, influence in state.claim_history:
            if pid == player_id and claim_count < 3:
                claims[claim_count] = influence.value
                claim_count += 1

        return claims
