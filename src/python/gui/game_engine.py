"""
Game engine for Coup - handles game flow and action resolution.

This module implements the game logic, mirroring the C++ apply_action
and related functions.
"""

import random
from typing import List, Union, Optional
from game_state import GameState, Action, Influence, ChallengeResponse
from rules import get_rules


class GameEngine:
    """
    Manages game flow and action resolution.

    Handles:
    - Action execution
    - Challenge resolution
    - Block resolution (for blocking variants)
    - Influence loss
    - Game state transitions
    """

    def __init__(self, variant: str):
        """
        Initialize game engine.

        Args:
            variant: Game variant ("simple", "simpleblocking", "base")
        """
        self.variant = variant
        self.rules = get_rules(variant)

    def create_initial_state(self) -> GameState:
        """
        Create initial game state with random card deals.

        Returns:
            Initial game state
        """
        # Create deck with appropriate cards
        deck = []
        for influence in self.rules.AVAILABLE_INFLUENCES:
            deck.extend([influence] * 2)  # 2 of each card type

        random.shuffle(deck)

        # Deal cards
        p1_influences = deck[:self.rules.STARTING_INFLUENCES]
        p2_influences = deck[self.rules.STARTING_INFLUENCES:2*self.rules.STARTING_INFLUENCES]

        state = GameState(
            variant=self.variant,
            max_influences=self.rules.MAX_INFLUENCES,
            p1_influences=p1_influences,
            p2_influences=p2_influences,
            p1_coins=self.rules.STARTING_COINS,
            p2_coins=self.rules.STARTING_COINS,
            current_player=1
        )

        return state

    def apply_action(self, state: GameState, action: Union[Action, ChallengeResponse]) -> GameState:
        """
        Apply an action to the state and return new state.

        Args:
            state: Current game state
            action: Action to apply

        Returns:
            New game state after action
        """
        new_state = state.clone()

        # Handle different action types
        if isinstance(action, ChallengeResponse):
            self._handle_challenge_response(new_state, action)
        elif isinstance(action, Action):
            self._handle_action(new_state, action)
        else:
            raise ValueError(f"Unknown action type: {type(action)}")

        return new_state

    def _handle_action(self, state: GameState, action: Action):
        """Handle a primary game action."""
        actor_id = state.current_player

        # Handle INCOME immediately (no challenge possible)
        if action == Action.INCOME:
            coins = state.get_player_coins(actor_id)
            state.set_player_coins(actor_id, coins + self.rules.INCOME_AMOUNT)
            state.current_player = 3 - actor_id
            return

        # Handle COUP immediately (no challenge possible)
        if action == Action.COUP:
            coins = state.get_player_coins(actor_id)
            state.set_player_coins(actor_id, coins - self.rules.COUP_COST)
            # Target loses influence
            target_id = 3 - actor_id
            self._lose_influence(state, target_id)
            state.current_player = 3 - target_id
            return

        # Challengeable actions - enter pending state
        if self.rules.is_challengeable(action):
            # ASSASSINATE costs 3 coins upfront (even if blocked or challenged)
            if action == Action.ASSASSINATE:
                coins = state.get_player_coins(actor_id)
                state.set_player_coins(actor_id, coins - self.rules.ASSASSINATE_COST)

            state.has_pending_action = True
            state.pending_action_actor = actor_id
            state.pending_action_type = action
            state.current_player = 3 - actor_id  # Opponent decides to challenge

            # Record claim in circular buffer
            required_influence = self.rules.get_required_influence(action)
            if required_influence:
                self._record_claim(state, actor_id, required_influence)

    def _handle_challenge_response(self, state: GameState, response: ChallengeResponse):
        """Handle challenge or pass response."""
        if state.has_pending_block_challenge:
            self._handle_block_challenge(state, response)
        elif state.has_pending_action:
            self._handle_action_challenge(state, response)

    def _handle_action_challenge(self, state: GameState, response: ChallengeResponse):
        """Handle challenge response to a pending action."""
        actor_id = state.pending_action_actor
        challenger_id = state.current_player
        action = state.pending_action_type

        if response == ChallengeResponse.PASS:
            # PASS - let the action happen (optimization: no longer transition to block state)
            state.has_pending_action = False
            self._execute_action(state, actor_id, action)
            state.current_player = 3 - actor_id

        elif response == ChallengeResponse.CHALLENGE:
            # Challenge! Check if actor has the required card
            state.has_pending_action = False
            required = self.rules.get_required_influence(action)

            actor_influences = state.get_player_influences(actor_id)
            has_card = required in actor_influences

            if has_card:
                # Actor has card - challenger loses influence
                self._lose_influence(state, challenger_id)

                # Actor succeeds with action (after exchanging card)
                # In real game, actor would exchange card back to deck
                # For simplicity, we skip the exchange

                # Execute action
                if not state.is_terminal():
                    self._execute_action(state, actor_id, action)

                state.current_player = 3 - actor_id
            else:
                # Actor doesn't have card - actor loses influence
                self._lose_influence(state, actor_id)

                # Action fails
                state.current_player = 3 - actor_id

        elif response == ChallengeResponse.BLOCK:
            # BLOCK - claim to block the action
            state.has_pending_action = False

            if not self.rules.is_blockable(action):
                # Error: tried to block unblockable action
                # Just execute the action anyway
                self._execute_action(state, actor_id, action)
                state.current_player = 3 - actor_id
            else:
                # Enter block challenge state
                state.has_pending_block_challenge = True
                state.pending_block_challenger = actor_id
                state.pending_block_claim = self.rules.get_blocking_influence(action)
                state.pending_block_action = action  # Save for potential execution if block fails

                # Record claim in circular buffer (blocker is current_player)
                self._record_claim(state, state.current_player, state.pending_block_claim)

                state.current_player = actor_id  # Actor can challenge the block

    def _handle_block_challenge(self, state: GameState, response: ChallengeResponse):
        """Handle challenge response to a block."""
        actor_id = state.pending_block_challenger
        blocker_id = 3 - actor_id
        action = state.pending_block_action
        claimed_card = state.pending_block_claim

        if response == ChallengeResponse.PASS:
            # No challenge to block - block succeeds, action doesn't happen
            state.has_pending_block_challenge = False
            state.current_player = 3 - actor_id

        elif response == ChallengeResponse.CHALLENGE:
            # Challenge the block!
            state.has_pending_block_challenge = False

            blocker_influences = state.get_player_influences(blocker_id)
            has_card = claimed_card in blocker_influences

            if has_card:
                # Blocker has card - challenger loses influence
                self._lose_influence(state, actor_id)
                # Block succeeds (action doesn't happen)
                # In real game, blocker would exchange card
                state.current_player = 3 - actor_id
            else:
                # Blocker doesn't have card - blocker loses influence
                self._lose_influence(state, blocker_id)
                # Block fails, action happens
                if not state.is_terminal():
                    self._execute_action(state, actor_id, action)
                state.current_player = 3 - actor_id

    def _execute_action(self, state: GameState, actor_id: int, action: Action):
        """Execute an action's effects."""
        target_id = 3 - actor_id

        if action == Action.TAX:
            coins = state.get_player_coins(actor_id)
            state.set_player_coins(actor_id, coins + self.rules.TAX_AMOUNT)

        elif action == Action.STEAL:
            actor_coins = state.get_player_coins(actor_id)
            target_coins = state.get_player_coins(target_id)
            steal_amt = min(self.rules.STEAL_AMOUNT, target_coins)
            state.set_player_coins(actor_id, actor_coins + steal_amt)
            state.set_player_coins(target_id, target_coins - steal_amt)

        elif action == Action.ASSASSINATE:
            # Cost already deducted when action was declared (in _handle_action)
            # Just handle the influence loss
            self._lose_influence(state, target_id)

        elif action == Action.INCOME:
            # Already handled in _handle_action
            pass

        elif action == Action.COUP:
            # Already handled in _handle_action
            pass

    def _record_claim(self, state: GameState, player_id: int, claimed_influence: Influence):
        """
        Record a claim in the circular buffer (matching C++ implementation).

        Args:
            state: Game state
            player_id: Player making the claim (1 or 2)
            claimed_influence: The influence being claimed
        """
        if player_id == 1:
            insert_pos = state.p1_claim_count % 3
            state.p1_claim_history[insert_pos] = claimed_influence.value
            state.p1_claim_count += 1
        else:
            insert_pos = state.p2_claim_count % 3
            state.p2_claim_history[insert_pos] = claimed_influence.value
            state.p2_claim_count += 1

    def _lose_influence(self, state: GameState, player_id: int):
        """Make a player lose an influence."""
        if player_id == 1:
            influences = state.p1_influences
        else:
            influences = state.p2_influences

        if len(influences) == 0:
            return  # No influence to lose

        # Remove first influence and add to revealed
        lost_card = influences.pop(0)
        state.revealed_cards.append(lost_card)

    def get_legal_actions(self, state: GameState, apply_pruning: bool = True) -> List[Union[Action, ChallengeResponse]]:
        """
        Get list of legal actions for current player.

        Args:
            state: Current game state
            apply_pruning: If True, apply perfect information pruning rules.
                          If False, return all legal options (for human players).

        Returns:
            List of legal actions
        """
        if state.has_pending_block_challenge:
            # Challenge-block decision
            if apply_pruning:
                must_challenge = self.rules.should_force_challenge_block(state, state.pending_block_claim)
                if must_challenge:
                    return [ChallengeResponse.CHALLENGE]
            return [ChallengeResponse.PASS, ChallengeResponse.CHALLENGE]

        elif state.has_pending_action:
            # Merged challenge/block decision (optimization!)
            is_challengeable = self.rules.is_challengeable(state.pending_action_type)
            is_blockable = self.rules.is_blockable(state.pending_action_type)
            must_challenge = False
            must_block = False

            if apply_pruning:
                if is_challengeable:
                    must_challenge = self.rules.should_force_challenge(state, state.pending_action_type)

                if is_blockable:
                    must_block = self.rules.should_force_block(state, state.pending_action_type)

                if must_challenge:
                    # Must challenge (all cards revealed)
                    # NOTE: Challenge has priority over block - resolves game state faster
                    return [ChallengeResponse.CHALLENGE]
                elif must_block:
                    # Must block (we have the blocking card - Contessa for Assassinate, Captain for Steal)
                    # NOTE: Only reached if must_challenge is False (challenge takes priority)
                    return [ChallengeResponse.BLOCK]

            # Return all legal options (either pruning didn't apply, or pruning is disabled)
            if is_blockable and not is_challengeable:
                # Blockable but not challengeable (unused in current variants)
                return [ChallengeResponse.PASS, ChallengeResponse.BLOCK]
            elif is_blockable and is_challengeable:
                # Both blockable and challengeable (e.g., STEAL, ASSASSINATE)
                return [ChallengeResponse.PASS, ChallengeResponse.CHALLENGE, ChallengeResponse.BLOCK]
            else:
                # Only challengeable (e.g., TAX)
                return [ChallengeResponse.PASS, ChallengeResponse.CHALLENGE]

        else:
            # Normal action selection
            return self.rules.get_legal_actions(state)

    def is_game_over(self, state: GameState) -> bool:
        """Check if game is over."""
        return state.is_terminal()

    def get_winner(self, state: GameState) -> Optional[int]:
        """Get winner (1 or 2) if game is over."""
        return state.get_winner()
