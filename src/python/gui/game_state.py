from enum import Enum
from dataclasses import dataclass
from copy import deepcopy
import random
from typing import Any, Type
from tqdm import tqdm

# ============================================================================
# ABSTRACTION MODE CONFIGURATION
# ============================================================================
# MUST MATCH C++ game_state.h configuration!

class AbstractionMode(Enum):
    NONE = 0          # No abstraction - use exact coins
    ASYMMETRIC = 1    # RECOMMENDED: Exact my coins, abstract opponent
    SYMMETRIC = 2     # Abstract both players (most aggressive)
    FINE_GRAINED = 3  # 5 buckets instead of 4

# IMPORTANT: Set this to match your C++ build!
ABSTRACTION_MODE = AbstractionMode.NONE

# Depth limit (must match C++ game_state.h line 101)
DEPTH_LIMIT = 20

# Early termination configuration (must match C++ game_state.h lines 117-125)
ENABLE_EARLY_TERMINATION = True
EARLY_TERM_MIN_DEPTH = 10
EARLY_TERM_SCORE_THRESHOLD = 14
INFLUENCE_VALUE = 10
COIN_VALUE = 1

# ============================================================================
# VARIANT CONFIGURATIONS
# ============================================================================

class VariantConfig:
    """Base class for variant-specific configurations"""
    MAX_INFLUENCES_PER_PLAYER: int
    STARTING_INFLUENCES: int
    NUM_INFLUENCE_TYPES: int
    STARTING_COINS: int
    COUP_COST: int
    ASSASSINATE_COST: int
    MUST_COUP_THRESHOLD: int
    STEAL_AMOUNT: int
    TAX_AMOUNT: int
    INCOME_AMOUNT: int
    FOREIGN_AID_AMOUNT: int = 0

    Influence: Type[Enum]
    Action: Type[Enum]

    @classmethod
    def get_variant_name(cls) -> str:
        raise NotImplementedError

class SimpleInfluence(Enum):
    DUKE = 0
    CAPTAIN = 1
    ASSASSIN = 2

class SimpleAction(Enum):
    INCOME = 0
    TAX = 1
    STEAL = 2
    COUP = 3  # No assassinate in simple variant

class SimpleAssassinAction(Enum):
    INCOME = 0
    TAX = 1
    STEAL = 2
    ASSASSINATE = 3
    COUP = 4

class BaseInfluence(Enum):
    DUKE = 0
    CAPTAIN = 1
    ASSASSIN = 2

class BaseAction(Enum):
    INCOME = 0
    TAX = 1
    STEAL = 2
    ASSASSINATE = 3
    COUP = 4

class FullInfluence(Enum):
    DUKE = 0
    ASSASSIN = 1
    CAPTAIN = 2
    AMBASSADOR = 3
    CONTESSA = 4

class FullAction(Enum):
    INCOME = 0
    FOREIGN_AID = 1
    TAX = 2
    STEAL = 3
    ASSASSINATE = 4
    EXCHANGE = 5
    COUP = 6

class SimpleCoupConfig(VariantConfig):
    """Simple Coup: 1 influence, 3 cards, 4 actions"""
    MAX_INFLUENCES_PER_PLAYER = 1
    STARTING_INFLUENCES = 1
    NUM_INFLUENCE_TYPES = 3
    STARTING_COINS = 2
    COUP_COST = 7
    ASSASSINATE_COST = 3
    MUST_COUP_THRESHOLD = 7
    STEAL_AMOUNT = 2
    TAX_AMOUNT = 3
    INCOME_AMOUNT = 1

    Influence = SimpleInfluence
    Action = SimpleAction

    @classmethod
    def get_variant_name(cls) -> str:
        return "SimpleCoup"

class SimpleAssassinCoupConfig(VariantConfig):
    """Simple Assassin Coup: 1 influence, 3 cards, 5 actions (WITH Assassinate)"""
    MAX_INFLUENCES_PER_PLAYER = 1
    STARTING_INFLUENCES = 1
    NUM_INFLUENCE_TYPES = 3
    STARTING_COINS = 2
    COUP_COST = 7
    ASSASSINATE_COST = 3
    MUST_COUP_THRESHOLD = 7
    STEAL_AMOUNT = 2
    TAX_AMOUNT = 3
    INCOME_AMOUNT = 1

    Influence = SimpleInfluence  # Same 3 card types as SimpleCoup
    Action = SimpleAssassinAction  # But with Assassinate action

    @classmethod
    def get_variant_name(cls) -> str:
        return "SimpleAssassinCoup"

class BaseCoupConfig(VariantConfig):
    """Base Coup: 2 influences, 3 cards, 5 actions"""
    MAX_INFLUENCES_PER_PLAYER = 2
    STARTING_INFLUENCES = 2
    NUM_INFLUENCE_TYPES = 3
    STARTING_COINS = 2
    COUP_COST = 7
    ASSASSINATE_COST = 3
    MUST_COUP_THRESHOLD = 10
    STEAL_AMOUNT = 2
    TAX_AMOUNT = 3
    INCOME_AMOUNT = 1

    Influence = BaseInfluence
    Action = BaseAction

    @classmethod
    def get_variant_name(cls) -> str:
        return "BaseCoup"

class FullCoupConfig(VariantConfig):
    """Full Coup: 2 influences, 5 cards, 7 actions"""
    MAX_INFLUENCES_PER_PLAYER = 2
    STARTING_INFLUENCES = 2
    NUM_INFLUENCE_TYPES = 5
    STARTING_COINS = 2
    COUP_COST = 7
    ASSASSINATE_COST = 3
    MUST_COUP_THRESHOLD = 10
    STEAL_AMOUNT = 2
    TAX_AMOUNT = 3
    INCOME_AMOUNT = 1
    FOREIGN_AID_AMOUNT = 2

    Influence = FullInfluence
    Action = FullAction

    @classmethod
    def get_variant_name(cls) -> str:
        return "FullCoup"

# Global variant configuration (set by initialize_variant)
CURRENT_VARIANT: Type[VariantConfig] = BaseCoupConfig

def initialize_variant(variant: str):
    """Initialize the global variant configuration"""
    global CURRENT_VARIANT
    variant_lower = variant.lower()
    if variant_lower == "simple":
        CURRENT_VARIANT = SimpleCoupConfig
    elif variant_lower == "simpleassassin":
        CURRENT_VARIANT = SimpleAssassinCoupConfig
    elif variant_lower == "base":
        CURRENT_VARIANT = BaseCoupConfig
    elif variant_lower == "full":
        CURRENT_VARIANT = FullCoupConfig
    else:
        raise ValueError(f"Unknown variant: {variant}. Must be 'simple', 'simpleassassin', 'base', or 'full'")

# ============================================================================
# ABSTRACTION FUNCTIONS (matching C++ implementation)
# ============================================================================

def abstract_coins_symmetric(coins: int) -> int:
    """4 buckets based on available actions"""
    if coins < 3: return 0   # Can't assassinate
    if coins < 7: return 1   # Can assassinate, can't coup
    if coins < 10: return 2  # Can coup (optional)
    return 3                 # Must coup

def abstract_coins_opponent(coins: int) -> int:
    """Coarser abstraction for opponent (3 buckets)"""
    if coins < 7: return 0   # Can't coup me
    if coins < 10: return 1  # Can coup me (optional)
    return 2                 # Must coup me

def abstract_coins_fine(coins: int) -> int:
    """5 buckets for finer-grained abstraction"""
    if coins < 3: return 0   # Can't assassinate
    if coins < 7: return 1   # Can assassinate
    if coins < 10: return 2  # Can coup (optional)
    if coins < 15: return 3  # Must coup (low)
    return 4                 # Must coup (high)

def abstract_my_coins(coins: int) -> int:
    """Abstract my coins based on mode"""
    if ABSTRACTION_MODE == AbstractionMode.NONE:
        return coins  # No abstraction
    elif ABSTRACTION_MODE == AbstractionMode.ASYMMETRIC:
        return coins  # Keep my coins exact!
    elif ABSTRACTION_MODE == AbstractionMode.SYMMETRIC:
        return abstract_coins_symmetric(coins)
    else:  # FINE_GRAINED
        return abstract_coins_fine(coins)

def abstract_opp_coins(coins: int) -> int:
    """Abstract opponent's coins based on mode"""
    if ABSTRACTION_MODE == AbstractionMode.NONE:
        return coins  # No abstraction
    elif ABSTRACTION_MODE == AbstractionMode.ASYMMETRIC:
        return abstract_coins_opponent(coins)  # Coarse abstraction
    elif ABSTRACTION_MODE == AbstractionMode.SYMMETRIC:
        return abstract_coins_symmetric(coins)
    else:  # FINE_GRAINED
        return abstract_coins_fine(coins)

def calculate_score(influence_count: int, coins: int) -> int:
    """Calculate game score for early termination"""
    return influence_count * INFLUENCE_VALUE + coins * COIN_VALUE

# ============================================================================

class ChallengeResponse(Enum):
    PASS = 0
    CHALLENGE = 1

@dataclass
class SimpleGameState:
    p1_influences: list[Any]  # List of Influence enums (variant-specific)
    p2_influences: list[Any]  # List of Influence enums (variant-specific)
    p1_coins: int
    p2_coins: int
    current_player: int  # 1 or 2
    deck: list[Any]  # List of Influence enums (variant-specific)
    revealed_cards: list[Any]  # List of Influence enums (variant-specific)
    pending_action: tuple = None  # (player, action) waiting for response
    p1_last_claim: Any | None = None  # Last influence claimed by P1 (variant-specific)
    p2_last_claim: Any | None = None  # Last influence claimed by P2 (variant-specific)
    depth: int = 0  # Track game depth

    def copy(self):
        return SimpleGameState(
            p1_influences=self.p1_influences.copy(),
            p2_influences=self.p2_influences.copy(),
            p1_coins=self.p1_coins,
            p2_coins=self.p2_coins,
            current_player=self.current_player,
            deck=self.deck.copy(),
            revealed_cards=self.revealed_cards.copy(),
            pending_action=self.pending_action,
            p1_last_claim=self.p1_last_claim,
            p2_last_claim=self.p2_last_claim,
            depth=self.depth
        )

    def is_terminal(self) -> bool:
        """Game ends when someone has no influences, depth exceeded, or overwhelming advantage"""
        # True terminal: One player has no influences
        if len(self.p1_influences) == 0 or len(self.p2_influences) == 0:
            return True

        # Depth limit reached
        # if self.depth >= DEPTH_LIMIT:
        #     return True

        # Smart Early Termination (MUST MATCH C++ game_state.cpp:30-67)
        # if ENABLE_EARLY_TERMINATION:
        #     if self.depth >= EARLY_TERM_MIN_DEPTH:
        #         p1_score = calculate_score(len(self.p1_influences), self.p1_coins)
        #         p2_score = calculate_score(len(self.p2_influences), self.p2_coins)
        #         score_diff = abs(p1_score - p2_score)

        #         # Terminate if advantage is overwhelming
        #         if score_diff >= EARLY_TERM_SCORE_THRESHOLD:
        #             return True

        #         # Additional heuristics for decided games (forced coup at 10+ coins!)

        #         # 1. Influence advantage with coup coins
        #         if (len(self.p1_influences) == 2 and len(self.p2_influences) == 1 and
        #             self.p1_coins >= 7):
        #             return True
        #         if (len(self.p2_influences) == 2 and len(self.p1_influences) == 1 and
        #             self.p2_coins >= 7):
        #             return True

        #         # 2. Both at 1 influence, one has must-coup coins (10+), other can't coup back
        #         if len(self.p1_influences) == 1 and len(self.p2_influences) == 1:
        #             if self.p1_coins >= 10 and self.p2_coins < 7:
        #                 return True
        #             if self.p2_coins >= 10 and self.p1_coins < 7:
        #                 return True

        return False

    def get_utility(self, player: int) -> float:
        """Utility for specified player - MUST MATCH C++ game_state.cpp:72-127"""
        # True terminal: Someone has no influences
        if len(self.p1_influences) == 0:
            return -1.0 if player == 1 else 1.0
        if len(self.p2_influences) == 0:
            return 1.0 if player == 1 else -1.0

        # Check if early termination applies (overwhelming advantage)
        # If so, award FULL utility to the winner!
        # if ENABLE_EARLY_TERMINATION:
        #     if self.depth >= EARLY_TERM_MIN_DEPTH:
        #         p1_score = calculate_score(len(self.p1_influences), self.p1_coins)
        #         p2_score = calculate_score(len(self.p2_influences), self.p2_coins)
        #         score_diff = abs(p1_score - p2_score)

        #         # Overwhelming score advantage → full utility
        #         if score_diff >= EARLY_TERM_SCORE_THRESHOLD:
        #             if p1_score > p2_score:
        #                 return 1.0 if player == 1 else -1.0  # P1 wins
        #             else:
        #                 return -1.0 if player == 1 else 1.0  # P2 wins

        #         # Influence advantage with coup coins → full utility
        #         if (len(self.p1_influences) == 2 and len(self.p2_influences) == 1 and
        #             self.p1_coins >= 7):
        #             return 1.0 if player == 1 else -1.0  # P1 wins

        #         if (len(self.p2_influences) == 2 and len(self.p1_influences) == 1 and
        #             self.p2_coins >= 7):
        #             return -1.0 if player == 1 else 1.0  # P2 wins

        #         # Must-coup advantage (1v1) → full utility
        #         if len(self.p1_influences) == 1 and len(self.p2_influences) == 1:
        #             if self.p1_coins >= 10 and self.p2_coins < 7:
        #                 return 1.0 if player == 1 else -1.0  # P1 wins (forced coup)
        #             if self.p2_coins >= 10 and self.p1_coins < 7:
        #                 return -1.0 if player == 1 else 1.0  # P2 wins (forced coup)

        # Depth limit reached (no early termination) → partial utility based on score
        # if self.depth >= DEPTH_LIMIT:
        #     p1_score = len(self.p1_influences) * 20 + self.p1_coins
        #     p2_score = len(self.p2_influences) * 20 + self.p2_coins
        #     result = (p1_score - p2_score) / 50.0
        #     return result if player == 1 else -result

        # Not terminal yet
        return 0.0

    def get_legal_actions(self, apply_pruning=True):
        """Get legal actions for current state

        Args:
            apply_pruning: If True, apply high-confidence pruning rules.
                          If False, return all legal actions (for human player).
        """
        coins = self.p1_coins if self.current_player == 1 else self.p2_coins
        Action = CURRENT_VARIANT.Action
        Influence = CURRENT_VARIANT.Influence

        # If waiting for challenge response
        if self.pending_action is not None:
            # High-Confidence Pruning for Challenge Responses (only if enabled)
            if apply_pruning:
                pending_player, pending_action = self.pending_action

                # Get information about the current player (responder)
                my_influences = self.p1_influences if self.current_player == 1 else self.p2_influences
                my_inf_count = len(my_influences)

                # Get required influence for the pending action
                required = get_required_influence(pending_action)

                # Count how many copies of the required card are revealed
                revealed_count_of_card = sum(1 for card in self.revealed_cards if card == required)

                # Count how many copies of the required card I hold
                my_count_of_card = sum(1 for card in my_influences if card == required)

                # Apply pruning rules
                must_challenge = False

                # Rule 1: One influence + Assassination → only CHALLENGE (if assassinate exists)
                if hasattr(Action, 'ASSASSINATE') and pending_action == Action.ASSASSINATE and my_inf_count == 1:
                    must_challenge = True

                # Rule 2: All required cards are revealed → only CHALLENGE
                # For simple/base (2 copies), for full (3 copies)
                max_copies = 2 if CURRENT_VARIANT.NUM_INFLUENCE_TYPES == 3 else 3
                if revealed_count_of_card >= max_copies:
                    must_challenge = True

                # Rule 3: I hold max copies → only CHALLENGE
                if my_count_of_card >= max_copies:
                    must_challenge = True

                # Rule 4: Copies held + revealed = max → only CHALLENGE
                if my_count_of_card + revealed_count_of_card >= max_copies:
                    must_challenge = True

                # Apply pruning
                if must_challenge:
                    return [ChallengeResponse.CHALLENGE]

            # If pruning is disabled or no pruning rule applies, return all options
            return list(ChallengeResponse)

        # Must coup at threshold
        if coins >= CURRENT_VARIANT.MUST_COUP_THRESHOLD:
            return [Action.COUP]

        # Normal action selection - build based on variant
        actions = [Action.INCOME, Action.TAX, Action.STEAL]

        # Add FOREIGN_AID for full variant
        if hasattr(Action, 'FOREIGN_AID'):
            actions.insert(1, Action.FOREIGN_AID)

        # Add EXCHANGE for full variant
        if hasattr(Action, 'EXCHANGE'):
            actions.append(Action.EXCHANGE)

        # Add ASSASSINATE if available and can afford
        if hasattr(Action, 'ASSASSINATE') and coins >= CURRENT_VARIANT.ASSASSINATE_COST:
            actions.append(Action.ASSASSINATE)

        # Add COUP if can afford
        if coins >= CURRENT_VARIANT.COUP_COST:
            actions.append(Action.COUP)

        return actions

    def get_info_set_key(self, player: int) -> str:
        """Information set from player's perspective - matches C++ hash"""
        if player == 1:
            my_inf = self.p1_influences.copy()
            my_coins = self.p1_coins
            opp_inf_count = len(self.p2_influences)
            opp_coins = self.p2_coins
            my_last_claim = self.p1_last_claim
            opp_last_claim = self.p2_last_claim
        else:
            my_inf = self.p2_influences.copy()
            my_coins = self.p2_coins
            opp_inf_count = len(self.p1_influences)
            opp_coins = self.p1_coins
            my_last_claim = self.p2_last_claim
            opp_last_claim = self.p1_last_claim

        # Sort influences for canonical representation
        my_inf_sorted = sorted([i.value for i in my_inf])

        # Sort revealed cards
        revealed_sorted = sorted([i.value for i in self.revealed_cards])

        # Build hash using FIXED-WIDTH bit packing
        # Total: 42 bits (22 unused out of 64)
        # See FIXED_WIDTH_ENCODING.md for complete layout
        hash_val = 1

        # Pack my influences (ALWAYS 2 slots of 4 bits, even for SimpleCoup)
        # Slots beyond MAX_INFLUENCES_PER_PLAYER are padded with 7
        for i in range(2):
            if i < CURRENT_VARIANT.MAX_INFLUENCES_PER_PLAYER and i < len(my_inf_sorted):
                hash_val = (hash_val << 4) | my_inf_sorted[i]
            else:
                hash_val = (hash_val << 4) | 7  # Empty slot

        # Pack coins with abstraction (ALWAYS 5 bits for both players)
        # Store exact coins (0-12) or abstracted bucket value (0-3)
        my_coins_hash = abstract_my_coins(my_coins)
        opp_coins_hash = abstract_opp_coins(opp_coins)
        hash_val = (hash_val << 5) | my_coins_hash
        hash_val = (hash_val << 5) | opp_coins_hash

        # Pack opponent influence count (2 bits)
        hash_val = (hash_val << 2) | opp_inf_count

        # Pack revealed cards (ALWAYS 4 slots of 3 bits)
        # Unused slots are padded with 7
        for i in range(4):
            if i < len(revealed_sorted):
                hash_val = (hash_val << 3) | revealed_sorted[i]
            else:
                hash_val = (hash_val << 3) | 7  # Empty slot

        # Pack pending action (ALWAYS 3 bits, 7 = no pending action)
        if self.pending_action is not None:
            pending_player, pending_action = self.pending_action
            hash_val = (hash_val << 3) | pending_action.value
        else:
            hash_val = (hash_val << 3) | 7  # No pending action

        # Pack both players' last claims (3 bits each, 7 = no claim)
        my_claim_bits = 7 if my_last_claim is None else my_last_claim.value
        opp_claim_bits = 7 if opp_last_claim is None else opp_last_claim.value
        hash_val = (hash_val << 3) | my_claim_bits
        hash_val = (hash_val << 3) | opp_claim_bits

        # Return as hex string to match C++ format
        return f"0x{hash_val:x}"

def apply_action(state: SimpleGameState, action) -> SimpleGameState:
    """Apply action and return new state"""
    next_state = state.copy()
    next_state.depth += 1
    Action = CURRENT_VARIANT.Action

    # Handle challenge response
    if isinstance(action, ChallengeResponse):
        return handle_challenge(next_state, action)

    # COUP and INCOME can't be challenged, execute immediately
    if action == Action.COUP:
        if next_state.current_player == 1:
            next_state.p1_coins -= CURRENT_VARIANT.COUP_COST
            lose_influence(next_state, 2)
        else:
            next_state.p2_coins -= CURRENT_VARIANT.COUP_COST
            lose_influence(next_state, 1)
        next_state.current_player = 3 - next_state.current_player

    elif action == Action.INCOME:
        if next_state.current_player == 1:
            next_state.p1_coins += CURRENT_VARIANT.INCOME_AMOUNT
        else:
            next_state.p2_coins += CURRENT_VARIANT.INCOME_AMOUNT
        next_state.current_player = 3 - next_state.current_player

    # Build list of challengeable actions based on variant
    else:
        # Determine if this action is challengeable
        is_challengeable = False
        if action == Action.TAX or action == Action.STEAL:
            is_challengeable = True
        elif hasattr(Action, 'ASSASSINATE') and action == Action.ASSASSINATE:
            is_challengeable = True
        elif hasattr(Action, 'EXCHANGE') and action == Action.EXCHANGE:
            is_challengeable = True

        if is_challengeable:
            # Challengeable action - set pending
            next_state.pending_action = (next_state.current_player, action)

            # Record the claim made by this player
            claimed = get_required_influence(action)
            if next_state.current_player == 1:
                next_state.p1_last_claim = claimed
            else:
                next_state.p2_last_claim = claimed

            next_state.current_player = 3 - next_state.current_player  # Switch to responder
        else:
            # Non-challengeable action (like FOREIGN_AID) - execute immediately
            execute_action(next_state, next_state.current_player, action)
            next_state.current_player = 3 - next_state.current_player

    return next_state

def handle_challenge(state: SimpleGameState, response: ChallengeResponse) -> SimpleGameState:
    """Handle challenge response to pending action"""
    next_state = state.copy()
    actor_id, action = next_state.pending_action
    challenger_id = next_state.current_player
    
    if response == ChallengeResponse.PASS:
        # No challenge - execute the action
        next_state.pending_action = None
        execute_action(next_state, actor_id, action)
        next_state.current_player = 3 - actor_id  # Switch back to opponent
        
    elif response == ChallengeResponse.CHALLENGE:
        # Challenge! Check if actor has the card
        next_state.pending_action = None
        required_card = get_required_influence(action)
        actor_influences = (next_state.p1_influences if actor_id == 1
                           else next_state.p2_influences)

        # print(f"\n[DEBUG] Challenge!")
        # print(f"  Actor: Player {actor_id}, Action: {action.name}, Needs: {required_card.name}")
        # print(f"  Actor's cards: {[card.name for card in actor_influences]}")
        # print(f"  Challenger: Player {challenger_id}")

        if required_card in actor_influences:
            # Actor has card - challenge fails
            # Challenger loses influence
            # print(f"  Result: Actor HAS {required_card.name} - Challenge FAILS")
            # print(f"  Player {challenger_id} (challenger) loses an influence")
            lose_influence(next_state, challenger_id)
            # Actor exchanges the revealed card
            actor_influences.remove(required_card)
            if len(next_state.deck) > 0:
                random.shuffle(next_state.deck)
                actor_influences.append(next_state.deck.pop())
                next_state.deck.append(required_card)
            # Action still happens
            execute_action(next_state, actor_id, action)
        else:
            # Actor doesn't have card - challenge succeeds
            # Actor loses influence, action fails
            lose_influence(next_state, actor_id)

        next_state.current_player = 3 - actor_id
    
    return next_state

def get_required_influence(action):
    """Get the influence required for an action"""
    Action = CURRENT_VARIANT.Action
    Influence = CURRENT_VARIANT.Influence

    if action == Action.TAX:
        return Influence.DUKE
    elif action == Action.STEAL:
        return Influence.CAPTAIN
    elif hasattr(Action, 'ASSASSINATE') and action == Action.ASSASSINATE:
        return Influence.ASSASSIN
    elif hasattr(Action, 'EXCHANGE') and action == Action.EXCHANGE:
        # For full coup, EXCHANGE requires AMBASSADOR
        return Influence.AMBASSADOR
    return None

def execute_action(state: SimpleGameState, player_id: int, action):
    """Execute an action's effect"""
    Action = CURRENT_VARIANT.Action

    if action == Action.TAX:
        if player_id == 1:
            state.p1_coins += CURRENT_VARIANT.TAX_AMOUNT
        else:
            state.p2_coins += CURRENT_VARIANT.TAX_AMOUNT

    elif action == Action.STEAL:
        if player_id == 1:
            steal_amount = min(CURRENT_VARIANT.STEAL_AMOUNT, state.p2_coins)
            state.p1_coins += steal_amount
            state.p2_coins -= steal_amount
        else:
            steal_amount = min(CURRENT_VARIANT.STEAL_AMOUNT, state.p1_coins)
            state.p2_coins += steal_amount
            state.p1_coins -= steal_amount

    elif hasattr(Action, 'ASSASSINATE') and action == Action.ASSASSINATE:
        # Cost coins and make opponent lose influence
        if player_id == 1:
            state.p1_coins -= CURRENT_VARIANT.ASSASSINATE_COST
            lose_influence(state, 2)  # Player 2 loses influence
        else:
            state.p2_coins -= CURRENT_VARIANT.ASSASSINATE_COST
            lose_influence(state, 1)  # Player 1 loses influence

    elif hasattr(Action, 'FOREIGN_AID') and action == Action.FOREIGN_AID:
        # FOREIGN_AID gives coins (Full Coup only)
        if player_id == 1:
            state.p1_coins += CURRENT_VARIANT.FOREIGN_AID_AMOUNT
        else:
            state.p2_coins += CURRENT_VARIANT.FOREIGN_AID_AMOUNT

    elif hasattr(Action, 'EXCHANGE') and action == Action.EXCHANGE:
        # EXCHANGE - simplified for now (Full Coup only)
        # In real game: draw 2 cards, choose which to keep
        # For CFR: just shuffle (simplified)
        pass

def lose_influence(state: SimpleGameState, player_id: int) -> Any | None:
    """Player loses an influence (random for now)"""
    influences = state.p1_influences if player_id == 1 else state.p2_influences

    if len(influences) == 0:
        return None

    # For CFR, just lose first card (or random)
    lost = influences.pop(0)
    state.revealed_cards.append(lost)
    return lost

def create_initial_state() -> SimpleGameState:
    """Create random starting state based on current variant"""
    Influence = CURRENT_VARIANT.Influence

    # Build deck based on variant
    deck = []
    if CURRENT_VARIANT.NUM_INFLUENCE_TYPES == 3:
        # Simple/Base Coup: 2 copies of each (Duke, Captain, Assassin)
        deck = [
            Influence.DUKE, Influence.DUKE,
            Influence.CAPTAIN, Influence.CAPTAIN,
            Influence.ASSASSIN, Influence.ASSASSIN
        ]
    elif CURRENT_VARIANT.NUM_INFLUENCE_TYPES == 5:
        # Full Coup: 3 copies of each (Duke, Assassin, Captain, Ambassador, Contessa)
        deck = [
            Influence.DUKE, Influence.DUKE, Influence.DUKE,
            Influence.ASSASSIN, Influence.ASSASSIN, Influence.ASSASSIN,
            Influence.CAPTAIN, Influence.CAPTAIN, Influence.CAPTAIN,
            Influence.AMBASSADOR, Influence.AMBASSADOR, Influence.AMBASSADOR,
            Influence.CONTESSA, Influence.CONTESSA, Influence.CONTESSA
        ]

    random.shuffle(deck)

    # Deal cards based on starting influences
    p1_infs = [deck.pop() for _ in range(CURRENT_VARIANT.STARTING_INFLUENCES)]
    p2_infs = [deck.pop() for _ in range(CURRENT_VARIANT.STARTING_INFLUENCES)]

    return SimpleGameState(
        p1_influences=p1_infs,
        p2_influences=p2_infs,
        p1_coins=CURRENT_VARIANT.STARTING_COINS,
        p2_coins=CURRENT_VARIANT.STARTING_COINS,
        current_player=1,
        deck=deck,
        revealed_cards=[],
        depth=0
    )

