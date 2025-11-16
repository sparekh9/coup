from enum import Enum
from dataclasses import dataclass
from copy import deepcopy
import random
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

class SimpleInfluence(Enum):
    DUKE = 0
    CAPTAIN = 1
    ASSASSIN = 2

class SimpleAction(Enum):
    INCOME = 0
    TAX = 1      # Claims Duke
    STEAL = 2    # Claims Captain
    ASSASSINATE = 3  # Claims Assassin
    COUP = 4

class ChallengeResponse(Enum):
    PASS = 0
    CHALLENGE = 1

@dataclass
class SimpleGameState:
    p1_influences: list[SimpleInfluence]
    p2_influences: list[SimpleInfluence]
    p1_coins: int
    p2_coins: int
    current_player: int  # 1 or 2
    deck: list[SimpleInfluence]
    revealed_cards: list[SimpleInfluence]
    pending_action: tuple = None  # (player, action) waiting for response
    p1_last_claim: SimpleInfluence | None = None  # Last influence claimed by P1
    p2_last_claim: SimpleInfluence | None = None  # Last influence claimed by P2
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
        if self.depth >= DEPTH_LIMIT:
            return True

        # Smart Early Termination (MUST MATCH C++ game_state.cpp:30-67)
        if ENABLE_EARLY_TERMINATION:
            if self.depth >= EARLY_TERM_MIN_DEPTH:
                p1_score = calculate_score(len(self.p1_influences), self.p1_coins)
                p2_score = calculate_score(len(self.p2_influences), self.p2_coins)
                score_diff = abs(p1_score - p2_score)

                # Terminate if advantage is overwhelming
                if score_diff >= EARLY_TERM_SCORE_THRESHOLD:
                    return True

                # Additional heuristics for decided games (forced coup at 10+ coins!)

                # 1. Influence advantage with coup coins
                if (len(self.p1_influences) == 2 and len(self.p2_influences) == 1 and
                    self.p1_coins >= 7):
                    return True
                if (len(self.p2_influences) == 2 and len(self.p1_influences) == 1 and
                    self.p2_coins >= 7):
                    return True

                # 2. Both at 1 influence, one has must-coup coins (10+), other can't coup back
                if len(self.p1_influences) == 1 and len(self.p2_influences) == 1:
                    if self.p1_coins >= 10 and self.p2_coins < 7:
                        return True
                    if self.p2_coins >= 10 and self.p1_coins < 7:
                        return True

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
        if ENABLE_EARLY_TERMINATION:
            if self.depth >= EARLY_TERM_MIN_DEPTH:
                p1_score = calculate_score(len(self.p1_influences), self.p1_coins)
                p2_score = calculate_score(len(self.p2_influences), self.p2_coins)
                score_diff = abs(p1_score - p2_score)

                # Overwhelming score advantage → full utility
                if score_diff >= EARLY_TERM_SCORE_THRESHOLD:
                    if p1_score > p2_score:
                        return 1.0 if player == 1 else -1.0  # P1 wins
                    else:
                        return -1.0 if player == 1 else 1.0  # P2 wins

                # Influence advantage with coup coins → full utility
                if (len(self.p1_influences) == 2 and len(self.p2_influences) == 1 and
                    self.p1_coins >= 7):
                    return 1.0 if player == 1 else -1.0  # P1 wins

                if (len(self.p2_influences) == 2 and len(self.p1_influences) == 1 and
                    self.p2_coins >= 7):
                    return -1.0 if player == 1 else 1.0  # P2 wins

                # Must-coup advantage (1v1) → full utility
                if len(self.p1_influences) == 1 and len(self.p2_influences) == 1:
                    if self.p1_coins >= 10 and self.p2_coins < 7:
                        return 1.0 if player == 1 else -1.0  # P1 wins (forced coup)
                    if self.p2_coins >= 10 and self.p1_coins < 7:
                        return -1.0 if player == 1 else 1.0  # P2 wins (forced coup)

        # Depth limit reached (no early termination) → partial utility based on score
        if self.depth >= DEPTH_LIMIT:
            p1_score = len(self.p1_influences) * 20 + self.p1_coins
            p2_score = len(self.p2_influences) * 20 + self.p2_coins
            result = (p1_score - p2_score) / 50.0
            return result if player == 1 else -result

        # Not terminal yet
        return 0.0

    def get_legal_actions(self, apply_pruning=True):
        """Get legal actions for current state

        Args:
            apply_pruning: If True, apply high-confidence pruning rules.
                          If False, return all legal actions (for human player).
        """
        coins = self.p1_coins if self.current_player == 1 else self.p2_coins

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

                # Rule 1: One influence + Assassination → only CHALLENGE
                if pending_action == SimpleAction.ASSASSINATE and my_inf_count == 1:
                    must_challenge = True

                # Rule 2: All required cards are revealed → only CHALLENGE
                if revealed_count_of_card == 2:
                    must_challenge = True

                # Rule 3: I hold both copies → only CHALLENGE
                if my_count_of_card == 2:
                    must_challenge = True

                # Rule 4: One copy held + one revealed → only CHALLENGE
                if my_count_of_card + revealed_count_of_card == 2:
                    must_challenge = True

                # Apply pruning
                if must_challenge:
                    return [ChallengeResponse.CHALLENGE]

            # If pruning is disabled or no pruning rule applies, return all options
            return list(ChallengeResponse)

        # Must coup at 10+ coins
        if coins >= 10:
            return [SimpleAction.COUP]

        # Normal action selection
        actions = [SimpleAction.INCOME, SimpleAction.TAX, SimpleAction.STEAL]

        if coins >= 3:
            actions.append(SimpleAction.ASSASSINATE)
        if coins >= 7:
            actions.append(SimpleAction.COUP)

        return actions

    def get_info_set_key(self, player: int) -> str:
        """Information set from player's perspective - matches C++ hash"""
        if player == 1:
            my_inf = self.p1_influences.copy()
            my_coins = self.p1_coins
            opp_inf_count = len(self.p2_influences)
            opp_coins = self.p2_coins
            opp_last_claim = self.p2_last_claim
        else:
            my_inf = self.p2_influences.copy()
            my_coins = self.p2_coins
            opp_inf_count = len(self.p1_influences)
            opp_coins = self.p1_coins
            opp_last_claim = self.p1_last_claim

        # Sort influences for canonical representation
        my_inf_sorted = sorted([i.value for i in my_inf])

        # Sort revealed cards
        revealed_sorted = sorted([i.value for i in self.revealed_cards])

        # Build hash using bit packing (matching C++ logic from game_state.cpp:79-106)
        hash_val = 0

        # Pack my influences (2 bits each, use 3 if no card)
        hash_val = (hash_val << 4) | (my_inf_sorted[0] if len(my_inf_sorted) > 0 else 3)
        print("After 1st inf:", bin(hash_val))
        hash_val = (hash_val << 4) | (my_inf_sorted[1] if len(my_inf_sorted) > 1 else 3)
        print("After 2st inf:", bin(hash_val))

        # Pack coins with abstraction (MUST MATCH C++ game_state.cpp:91-109)
        my_coins_hash = abstract_my_coins(my_coins)
        opp_coins_hash = abstract_opp_coins(opp_coins)

        # Bit width depends on abstraction mode
        if ABSTRACTION_MODE in (AbstractionMode.NONE, AbstractionMode.ASYMMETRIC):
            hash_val = (hash_val << 5) | my_coins_hash  # Exact: 5 bits
        else:
            hash_val = (hash_val << 2) | my_coins_hash  # Abstracted: 2 bits

        if ABSTRACTION_MODE == AbstractionMode.NONE:
            hash_val = (hash_val << 5) | opp_coins_hash  # Exact: 5 bits
        else:
            hash_val = (hash_val << 2) | opp_coins_hash  # Abstracted: 2 bits

        # Pack opponent influence count (2 bits)
        hash_val = (hash_val << 2) | opp_inf_count

        # Pack revealed cards (3 bits for count, then 2 bits each card)
        hash_val = (hash_val << 3) | len(revealed_sorted)
        for i in range(min(len(revealed_sorted), 4)):
            hash_val = (hash_val << 2) | revealed_sorted[i]

        # Pack pending action info
        if self.pending_action is not None:
            hash_val = (hash_val << 1) | 1
            pending_player, pending_action = self.pending_action
            hash_val = (hash_val << 1) | (pending_player - 1)
            hash_val = (hash_val << 2) | pending_action.value
        else:
            hash_val = (hash_val << 1) | 0

        # Pack opponent's last claim (2 bits)
        # None -> 3 (no claim), otherwise use influence value (0=Duke, 1=Captain, 2=Assassin)
        claim_bits = 3 if opp_last_claim is None else opp_last_claim.value
        hash_val = (hash_val << 2) | claim_bits

        # Return as hex string to match C++ format
        return f"0x{hash_val:x}"

def apply_action(state: SimpleGameState, action: SimpleAction) -> SimpleGameState:
    """Apply action and return new state"""
    next_state = state.copy()
    next_state.depth += 1

    # Handle challenge response
    if isinstance(action, ChallengeResponse):
        return handle_challenge(next_state, action)

    # Handle normal action
    assert isinstance(action, SimpleAction)

    # COUP and INCOME can't be challenged, execute immediately
    if action == SimpleAction.COUP:
        if next_state.current_player == 1:
            next_state.p1_coins -= 7
            lose_influence(next_state, 2)
        else:
            next_state.p2_coins -= 7
            lose_influence(next_state, 1)
        next_state.current_player = 3 - next_state.current_player
        
    elif action == SimpleAction.INCOME:
        if next_state.current_player == 1:
            next_state.p1_coins += 1
        else:
            next_state.p2_coins += 1
        next_state.current_player = 3 - next_state.current_player
    
    # TAX, STEAL, and ASSASSINATE can be challenged - set pending
    elif action in [SimpleAction.TAX, SimpleAction.STEAL, SimpleAction.ASSASSINATE]:
        next_state.pending_action = (next_state.current_player, action)

        # Record the claim made by this player
        claimed = get_required_influence(action)
        if next_state.current_player == 1:
            next_state.p1_last_claim = claimed
        else:
            next_state.p2_last_claim = claimed

        next_state.current_player = 3 - next_state.current_player  # Switch to responder

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
        
        if required_card in actor_influences:
            # Actor has card - challenge fails
            # Challenger loses influence
            lose_influence(next_state, challenger_id)
            # Actor exchanges the revealed card
            actor_influences.remove(required_card)
            if len(next_state.deck) > 0:
                next_state.deck.append(required_card)
                random.shuffle(next_state.deck)
                actor_influences.append(next_state.deck.pop())
            # Action still happens
            execute_action(next_state, actor_id, action)
        else:
            # Actor doesn't have card - challenge succeeds
            # Actor loses influence, action fails
            lose_influence(next_state, actor_id)
        
        next_state.current_player = 3 - actor_id
    
    return next_state

def get_required_influence(action: SimpleAction) -> SimpleInfluence:
    """Get the influence required for an action"""
    if action == SimpleAction.TAX:
        return SimpleInfluence.DUKE
    elif action == SimpleAction.STEAL:
        return SimpleInfluence.CAPTAIN
    elif action == SimpleAction.ASSASSINATE:
        return SimpleInfluence.ASSASSIN
    return None

def execute_action(state: SimpleGameState, player_id: int, action: SimpleAction):
    """Execute an action's effect"""
    if action == SimpleAction.TAX:
        if player_id == 1:
            state.p1_coins += 3
        else:
            state.p2_coins += 3

    elif action == SimpleAction.STEAL:
        if player_id == 1:
            steal_amount = min(2, state.p2_coins)
            state.p1_coins += steal_amount
            state.p2_coins -= steal_amount
        else:
            steal_amount = min(2, state.p1_coins)
            state.p2_coins += steal_amount
            state.p1_coins -= steal_amount

    elif action == SimpleAction.ASSASSINATE:
        # Cost 3 coins and make opponent lose influence
        if player_id == 1:
            state.p1_coins -= 3
            lose_influence(state, 2)  # Player 2 loses influence
        else:
            state.p2_coins -= 3
            lose_influence(state, 1)  # Player 1 loses influence

def lose_influence(state: SimpleGameState, player_id: int) -> SimpleInfluence | None:
    """Player loses an influence (random for now)"""
    influences = state.p1_influences if player_id == 1 else state.p2_influences
    
    if len(influences) == 0:
        return None
    
    # For CFR, just lose first card (or random)
    lost = influences.pop(0)
    state.revealed_cards.append(lost)
    return lost

def create_initial_state() -> SimpleGameState:
    """Create random starting state with 6-card deck"""
    deck = [SimpleInfluence.DUKE, SimpleInfluence.DUKE,
            SimpleInfluence.CAPTAIN, SimpleInfluence.CAPTAIN,
            SimpleInfluence.ASSASSIN, SimpleInfluence.ASSASSIN]
    random.shuffle(deck)
    
    return SimpleGameState(
        p1_influences=[deck.pop(), deck.pop()],
        p2_influences=[deck.pop(), deck.pop()],
        p1_coins=2,
        p2_coins=2,
        current_player=1,
        deck=deck,
        revealed_cards=[],
        depth=0
    )

import json
from collections import defaultdict

class SimpleCFRTrainer:
    def __init__(self):
        self.regret_sum = defaultdict(lambda: defaultdict(float))
        self.strategy_sum = defaultdict(lambda: defaultdict(float))
    
    def get_strategy(self, info_set_key: str, actions: list) -> dict:
        """Regret matching"""
        regrets = self.regret_sum[info_set_key]
        positive_regrets = {a: max(0, regrets[a]) for a in actions}
        regret_sum = sum(positive_regrets.values())
        
        if regret_sum > 0:
            return {a: positive_regrets[a] / regret_sum for a in actions}
        else:
            return {a: 1.0 / len(actions) for a in actions}
    
    def get_average_strategy(self, info_set_key: str, actions: list) -> dict:
        """Get average strategy over all iterations"""
        strategy = self.strategy_sum[info_set_key]
        total = sum(strategy.values())
        
        if total > 0:
            return {a: strategy.get(a, 0) / total for a in actions}
        else:
            return {a: 1.0 / len(actions) for a in actions}
    
    def save_strategy(self, filename: str):
        """Save trained average strategies to JSON"""
        strategies = {}
        
        # Convert strategy_sum to average strategies
        for info_set_key in self.strategy_sum.keys():
            actions_dict = self.strategy_sum[info_set_key]
            
            # Get actions (could be SimpleAction or ChallengeResponse)
            actions = list(actions_dict.keys())
            avg_strat = self.get_average_strategy(info_set_key, actions)
            
            # Convert enum actions to strings for JSON serialization
            strategies[info_set_key] = {
                self._action_to_str(action): prob 
                for action, prob in avg_strat.items()
            }
        
        # Save to JSON
        with open(filename, 'w') as f:
            json.dump(strategies, f, indent=2)
        
        print(f"Saved {len(strategies)} information sets to {filename}")
    
    def load_strategy(self, filename: str):
        """Load trained strategies from JSON (handles both Python and C++ formats)"""
        with open(filename, 'r') as f:
            data = json.load(f)

        # Clear existing strategies
        self.strategy_sum.clear()

        # Load strategies and convert back to enum actions
        for info_set_key, strategy in data.items():
            # info_set_key is already a hex string (e.g., "0x1a2b3c4d")
            for action_str, prob in strategy.items():
                action = self._str_to_action(action_str)
                # Store the cumulative probability (for average strategy calculation)
                # Note: C++ saves normalized probabilities, so we use them directly
                self.strategy_sum[info_set_key][action] = prob

        print(f"Loaded {len(data)} information sets from {filename}")
    
    def _action_to_str(self, action) -> str:
        """Convert action enum to string (matches C++ format)"""
        if isinstance(action, SimpleAction):
            return action.name  # Just "INCOME", not "SimpleAction.INCOME"
        elif isinstance(action, ChallengeResponse):
            return action.name  # Just "PASS", not "ChallengeResponse.PASS"
        else:
            return str(action)
    
    def _str_to_action(self, action_str: str):
        """Convert string back to action enum (handles both Python and C++ formats)"""
        if action_str.startswith("SimpleAction."):
            # Python format: "SimpleAction.INCOME"
            action_name = action_str.split(".")[1]
            return SimpleAction[action_name]
        elif action_str.startswith("ChallengeResponse."):
            # Python format: "ChallengeResponse.PASS"
            action_name = action_str.split(".")[1]
            return ChallengeResponse[action_name]
        else:
            # C++ format: just "INCOME", "PASS", etc.
            # Try SimpleAction first
            try:
                return SimpleAction[action_str]
            except KeyError:
                pass
            # Try ChallengeResponse
            try:
                return ChallengeResponse[action_str]
            except KeyError:
                raise ValueError(f"Unknown action string: {action_str}")
    
    def train(self, iterations: int):
        for i in tqdm(range(iterations)):
            state = create_initial_state()
            self.cfr(state, 1, 1.0, 1.0)
            state = create_initial_state()
            self.cfr(state, 2, 1.0, 1.0)
    
    def cfr(self, state: SimpleGameState, traversing_player: int,
            reach_p1: float, reach_p2: float) -> float:
        
        if state.is_terminal():
            return state.get_utility(1)
        
        current_player = state.current_player
        info_set_key = state.get_info_set_key(current_player)
        actions = state.get_legal_actions()
        strategy = self.get_strategy(info_set_key, actions)
        
        # Update strategy sum
        reach_prob = reach_p1 if current_player == 1 else reach_p2
        for action in actions:
            self.strategy_sum[info_set_key][action] += reach_prob * strategy[action]
        
        # Compute utilities
        action_utilities = {}
        for action in actions:
            next_state = apply_action(state, action)
            
            if current_player == 1:
                action_utilities[action] = self.cfr(
                    next_state, traversing_player,
                    reach_p1 * strategy[action], reach_p2
                )
            else:
                action_utilities[action] = self.cfr(
                    next_state, traversing_player,
                    reach_p1, reach_p2 * strategy[action]
                )
        
        expected_utility = sum(strategy[a] * action_utilities[a] for a in actions)
        
        # Update regrets
        if current_player == traversing_player:
            opponent_reach = reach_p2 if current_player == 1 else reach_p1
            for action in actions:
                regret = action_utilities[action] - expected_utility
                self.regret_sum[info_set_key][action] += opponent_reach * regret
        
        return expected_utility

import tkinter as tk
from tkinter import ttk, messagebox
import random

class CoupBot:
    """Bot that uses trained CFR strategy"""
    def __init__(self, trainer: SimpleCFRTrainer):
        self.trainer = trainer
    
    def choose_action(self, state: SimpleGameState, player_id: int):
        """Choose action based on learned strategy"""
        info_set_key = state.get_info_set_key(player_id)
        actions = state.get_legal_actions()
        
        # Get strategy from trainer
        strategy = self.trainer.get_average_strategy(info_set_key, actions)
        
        # Sample action based on probabilities
        actions_list = list(strategy.keys())
        probabilities = [strategy[a] for a in actions_list]
        
        # Normalize probabilities (in case of floating point errors)
        total = sum(probabilities)
        if total > 0:
            probabilities = [p / total for p in probabilities]
        else:
            probabilities = [1.0 / len(actions_list)] * len(actions_list)
        
        chosen_action = random.choices(actions_list, weights=probabilities)[0]
        return chosen_action, strategy, info_set_key

class CoupGUI:
    def __init__(self, root, trainer: SimpleCFRTrainer, human_player=1):
        self.root = root
        self.root.title("Coup vs Bot")
        self.root.geometry("800x600")
        
        self.trainer = trainer
        self.bot = CoupBot(trainer)
        self.human_player = human_player
        self.bot_player = 3 - human_player
        
        self.state = None
        self.game_over = False
        
        self.setup_ui()
        self.new_game()
    
    def setup_ui(self):
        """Create the UI elements"""
        # Top frame - Opponent info
        self.opponent_frame = ttk.LabelFrame(self.root, text="Opponent (Bot)", padding=10)
        self.opponent_frame.pack(fill=tk.X, padx=10, pady=5)
        
        self.opponent_info = tk.Label(self.opponent_frame, text="", font=("Arial", 12))
        self.opponent_info.pack()
        
        # Middle frame - Game log
        self.log_frame = ttk.LabelFrame(self.root, text="Game Log", padding=10)
        self.log_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)
        
        self.log_text = tk.Text(self.log_frame, height=10, width=80, state=tk.DISABLED)
        self.log_text.pack(fill=tk.BOTH, expand=True)
        
        scrollbar = ttk.Scrollbar(self.log_text)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        self.log_text.config(yscrollcommand=scrollbar.set)
        scrollbar.config(command=self.log_text.yview)
        
        # Bottom frame - Player info and actions
        self.player_frame = ttk.LabelFrame(self.root, text="You", padding=10)
        self.player_frame.pack(fill=tk.X, padx=10, pady=5)
        
        self.player_info = tk.Label(self.player_frame, text="", font=("Arial", 12, "bold"))
        self.player_info.pack()
        
        # Action buttons frame
        self.action_frame = ttk.Frame(self.player_frame)
        self.action_frame.pack(pady=10)
        
        # Control buttons
        self.control_frame = ttk.Frame(self.root)
        self.control_frame.pack(fill=tk.X, padx=10, pady=5)
        
        ttk.Button(self.control_frame, text="New Game", command=self.new_game).pack(side=tk.LEFT, padx=5)
        ttk.Button(self.control_frame, text="Decode Info Set", command=self.decode_info_set_dialog).pack(side=tk.LEFT, padx=5)
        ttk.Button(self.control_frame, text="Quit", command=self.root.quit).pack(side=tk.LEFT, padx=5)
    
    def new_game(self):
        """Start a new game"""
        self.state = create_initial_state()
        self.game_over = False

        self.log_text.config(state=tk.NORMAL)
        self.log_text.delete(1.0, tk.END)
        self.log_text.config(state=tk.DISABLED)

        self.log(f"New game started! You are Player {self.human_player}")
        self.log(f"Your cards: {self.get_player_cards(self.human_player)}")

        self.update_display()

        # If bot goes first, make its move
        if self.state.current_player == self.bot_player:
            self.root.after(1000, self.bot_turn)

    def update_display(self):
        """Update all display elements"""
        if self.game_over:
            return

        # Update opponent info
        bot_inf_count = len(self.state.p2_influences) if self.bot_player == 2 else len(self.state.p1_influences)
        bot_coins = self.state.p2_coins if self.bot_player == 2 else self.state.p1_coins
        self.opponent_info.config(text=f"Influences: {bot_inf_count}  |  Coins: {bot_coins}")

        # Update player info
        human_cards = self.get_player_cards(self.human_player)
        human_coins = self.state.p1_coins if self.human_player == 1 else self.state.p2_coins
        self.player_info.config(text=f"Your Cards: {human_cards}  |  Coins: {human_coins}")

        # Update action buttons
        self.update_action_buttons()

    def get_player_cards(self, player_id):
        """Get string representation of player's cards"""
        influences = self.state.p1_influences if player_id == 1 else self.state.p2_influences
        return ", ".join([inf.name for inf in influences])

    def update_action_buttons(self):
        """Create/update action buttons"""
        # Clear existing buttons
        for widget in self.action_frame.winfo_children():
            widget.destroy()

        if self.state.current_player != self.human_player or self.game_over:
            return

        # Get ALL legal actions for human player (no pruning)
        actions = self.state.get_legal_actions(apply_pruning=False)

        for action in actions:
            btn = ttk.Button(
                self.action_frame,
                text=self.get_action_text(action),
                command=lambda a=action: self.human_action(a)
            )
            btn.pack(side=tk.LEFT, padx=5)

    def get_action_text(self, action: SimpleAction | ChallengeResponse) -> str:
        """Get display text for action"""
        if isinstance(action, SimpleAction):
            if action == SimpleAction.INCOME:
                return "Income (+1 coin)"
            elif action == SimpleAction.TAX:
                return "Tax (+3 coins, Duke)"
            elif action == SimpleAction.STEAL:
                return "Steal (2 coins, Captain)"
            elif action == SimpleAction.ASSASSINATE:
                return "Assassinate (-3 coins, Assassin)"
            elif action == SimpleAction.COUP:
                return "Coup (-7 coins)"
        else:
            if action == ChallengeResponse.PASS:
                return "Pass (allow action)"
            elif action == ChallengeResponse.CHALLENGE:
                return "Challenge!"
    
    def human_action(self, action: SimpleAction | ChallengeResponse):
        """Handle human player action"""
        self.log(f"You: {action.name}")

        # Apply action
        self.state = apply_action(self.state, action)

        # DEBUG: Log state after action
        self.log(f"  -> After action: P1={len(self.state.p1_influences)} inf, P2={len(self.state.p2_influences)} inf, Current player={self.state.current_player}, Depth={self.state.depth}")

        # Check if game ended
        if self.state.is_terminal():
            self.log(f"  -> Game is terminal!")
            self.end_game()
            return

        self.update_display()

        # If it's bot's turn now, schedule bot move
        if self.state.current_player == self.bot_player:
            self.log(f"  -> Bot's turn next")
            self.root.after(1000, self.bot_turn)
        else:
            self.log(f"  -> Your turn next")    

    def bot_turn(self):
        """Bot makes a move"""
        if self.game_over:
            self.log("  -> Bot turn cancelled (game over)")
            return

        self.log(f"Bot's turn (Player {self.bot_player})")

        action, strategy, infosetkey = self.bot.choose_action(self.state, self.bot_player)

        # Log bot's decision with probabilities
        strategy_str = ", ".join([f"{a.name}: {p:.2f}" for a, p in sorted(strategy.items(), key=lambda x: -x[1])[:3]])
        self.log(f"Bot: {action.name} (strategy: {strategy_str}, infoset:{infosetkey})")

        # Apply action
        self.state = apply_action(self.state, action)

        # DEBUG: Log state after action
        self.log(f"  -> After action: P1={len(self.state.p1_influences)} inf, P2={len(self.state.p2_influences)} inf, Current player={self.state.current_player}, Depth={self.state.depth}")

        # Check if game ended
        if self.state.is_terminal():
            self.log(f"  -> Game is terminal!")
            self.end_game()
            return

        self.update_display()

        # Check if it's still bot's turn (shouldn't normally happen, but check anyway)
        if self.state.current_player == self.bot_player:
            self.log(f"  -> Still bot's turn, scheduling another move")
            self.root.after(1000, self.bot_turn)
        else:
            self.log(f"  -> Your turn next")    

    def end_game(self):
        """Handle game end"""
        self.game_over = True

        # Clear action buttons
        for widget in self.action_frame.winfo_children():
            widget.destroy()

        # Determine winner
        utility = self.state.get_utility(self.human_player)
        if utility > 0:
            winner = "You win!"
            self.log("\n🎉 YOU WIN! 🎉")
        elif utility < 0:
            winner = "Bot wins!"
            self.log("\n😢 Bot wins!")
        else:
            winner = "Draw!"
            self.log("\nGame ended in a draw")

        messagebox.showinfo("Game Over", winner)

    def decode_info_set_dialog(self):
        """Prompt user for hex string and decode it"""
        # Create input dialog
        popup = tk.Toplevel(self.root)
        popup.title("Decode Information Set")
        popup.geometry("400x150")

        ttk.Label(popup, text="Enter hex string (e.g., 0x1a2b3c):").pack(pady=10)

        entry = ttk.Entry(popup, width=40)
        entry.pack(pady=5)

        # Pre-fill with bot's current info set if available
        if not self.game_over:
            current_key = self.state.get_info_set_key(self.bot_player)
            entry.insert(0, current_key)

        def decode_and_show():
            hex_string = entry.get().strip()
            try:
                decoded = self.decode_from_hash(hex_string)
                popup.destroy()
                self.show_decoded_info_set(hex_string, decoded)
            except Exception as e:
                messagebox.showerror("Decode Error", f"Failed to decode: {str(e)}")

        ttk.Button(popup, text="Decode", command=decode_and_show).pack(pady=10)
        ttk.Button(popup, text="Cancel", command=popup.destroy).pack(pady=5)

    def decode_from_hash(self, info_set_key: str) -> dict:
        """Decode an information set hash by parsing binary string left to right

        MUST MATCH C++ game_state.cpp lines 129-217!

        Encoding order (LEFT to RIGHT in binary string):
        1. My influences (2 cards, 4 bits each) = 8 bits
        2. My coins (5 or 2 bits depending on mode)
        3. Opp coins (5 or 2 bits depending on mode)
        4. Opp influence count (2 bits)
        5. Revealed count (3 bits) + cards (2 bits each, 0-4 cards)
        6. Pending flag (1 bit) + if yes: player (1 bit) + action (2 bits)
        7. Opponent's last claim (2 bits)
        """
        # Convert hex to int to binary string (without '0b' prefix)
        hash_val = int(info_set_key, 16)
        bin_str = bin(hash_val)[2:]  # Remove '0b' prefix

        print(f"Hash: {info_set_key}")
        print(f"Binary: {bin_str}")
        print(f"Length: {len(bin_str)} bits")

        decoded = {}
        pos = 0  # Current position in binary string (reading left to right)

        # Helper function to extract n bits from current position
        def extract_bits(n):
            nonlocal pos
            if pos + n > len(bin_str):
                return 0  # Return 0 if we're past the end
            bits = bin_str[pos:pos + n]
            pos += n
            value = int(bits, 2) if bits else 0
            print(f"  Extracted {n} bits at pos {pos-n}: '{bits}' = {value}")
            return value

        # 1. My influences (2 cards, 4 bits each)
        print("\n1. My influences:")
        inf1_bits = extract_bits(4)
        inf2_bits = extract_bits(4)

        my_infs = []
        if inf1_bits != 3:
            inf1_bits >>= 2
            my_infs.append(SimpleInfluence(inf1_bits).name)
        if inf2_bits != 3:
            inf2_bits >>= 2
            my_infs.append(SimpleInfluence(inf2_bits).name)
        decoded["My Cards"] = ", ".join(my_infs) if my_infs else "None"

        # 2. My coins
        print("\n2. My coins:")
        if ABSTRACTION_MODE in (AbstractionMode.NONE, AbstractionMode.ASYMMETRIC):
            my_coins = extract_bits(5)
            decoded["My Coins"] = f"{my_coins} coins"
        else:
            my_coins_bucket = extract_bits(2)
            decoded["My Coins"] = f"Bucket {my_coins_bucket}"

        # 3. Opponent coins
        print("\n3. Opponent coins:")
        if ABSTRACTION_MODE == AbstractionMode.NONE:
            opp_coins = extract_bits(5)
            decoded["Opponent Coins"] = f"{opp_coins} coins"
        else:
            opp_coins_bucket = extract_bits(2)
            decoded["Opponent Coins"] = f"Bucket {opp_coins_bucket}"

        # 4. Opponent influence count
        print("\n4. Opponent influence count:")
        opp_inf_count = extract_bits(2)
        decoded["Opponent Influences"] = f"{opp_inf_count} cards"

        # 5. Revealed cards
        print("\n5. Revealed cards:")
        revealed_count = extract_bits(3)
        print(f"  Revealed count: {revealed_count}")

        revealed_cards = []
        for i in range(revealed_count):
            card_bits = extract_bits(2)
            revealed_cards.append(SimpleInfluence(card_bits).name)
        decoded["Revealed Cards"] = ", ".join(revealed_cards) if revealed_cards else "None"

        # 6. Pending action
        print("\n6. Pending action:")
        has_pending = extract_bits(1)
        print(f"  Has pending: {has_pending}")

        if has_pending:
            pending_player_bit = extract_bits(1)
            pending_action_bits = extract_bits(2)
            pending_player = pending_player_bit + 1
            pending_action = SimpleAction(pending_action_bits)
            decoded["Pending Action"] = f"Player {pending_player} claimed {pending_action.name}"
        else:
            decoded["Pending Action"] = "None"

        # 7. Opponent's last claim
        print("\n7. Opponent's last claim:")
        claim_bits = extract_bits(2)
        if claim_bits == 3:
            decoded["Opponent's Last Claim"] = "None"
        else:
            decoded["Opponent's Last Claim"] = SimpleInfluence(claim_bits).name

        # Add metadata
        decoded["Abstraction Mode"] = str(ABSTRACTION_MODE).split('.')[-1]

        print(f"\nFinal position: {pos}/{len(bin_str)} bits")

        return decoded

    def show_decoded_info_set(self, hex_string: str, decoded: dict):
        """Display decoded information set in a popup"""
        popup = tk.Toplevel(self.root)
        popup.title("Decoded Information Set")
        popup.geometry("600x700")

        # Add scrollable text widget
        frame = ttk.Frame(popup, padding=10)
        frame.pack(fill=tk.BOTH, expand=True)

        text = tk.Text(frame, wrap=tk.WORD, font=("Courier", 10))
        text.pack(fill=tk.BOTH, expand=True, side=tk.LEFT)

        scrollbar = ttk.Scrollbar(frame, command=text.yview)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        text.config(yscrollcommand=scrollbar.set)

        # Display decoded information
        text.insert(tk.END, f"=== Decoded Information Set ===\n\n")
        text.insert(tk.END, f"Hash Key: {hex_string}\n\n")
        text.insert(tk.END, "=" * 50 + "\n\n")

        for key, value in decoded.items():
            text.insert(tk.END, f"{key}:\n  {value}\n\n")

        text.insert(tk.END, "=" * 50 + "\n\n")
        text.insert(tk.END, "STRATEGY:\n")

        # Check if strategy exists for this info set
        if hex_string in self.trainer.strategy_sum:
            text.insert(tk.END, "Learned strategy for this info set:\n\n")

            # Get the average strategy
            actions_dict = self.trainer.strategy_sum[hex_string]
            actions = list(actions_dict.keys())
            avg_strategy = self.trainer.get_average_strategy(hex_string, actions)

            # Sort by probability (highest first)
            sorted_actions = sorted(avg_strategy.items(), key=lambda x: -x[1])

            for action, prob in sorted_actions:
                text.insert(tk.END, f"  {action.name}: {prob:.4f} ({prob*100:.2f}%)\n")
        else:
            text.insert(tk.END, "No learned strategy for this info set.\n")

        text.config(state=tk.DISABLED)

        # Close button
        ttk.Button(popup, text="Close", command=popup.destroy).pack(pady=10)

    def log(self, message: str) -> None:
        """Add message to game log"""
        self.log_text.config(state=tk.NORMAL)
        self.log_text.insert(tk.END, message + "\n")
        self.log_text.see(tk.END)
        self.log_text.config(state=tk.DISABLED)

def play_gui(strategy_file: str="coup_strategy.json", human_player:int=1):
    """Launch the GUI"""
    # Load trained strategy
    trainer = SimpleCFRTrainer()

    try:
        trainer.load_strategy(strategy_file)
        print(f"Loaded strategy from {strategy_file}")
    except FileNotFoundError:
        print(f"Strategy file {strategy_file} not found. Training a new bot...")
        trainer.train(10000)
        trainer.save_strategy(strategy_file)
        print("Training complete!")

    # Create GUI
    root = tk.Tk()
    gui = CoupGUI(root, trainer, human_player)
    root.mainloop()

def play_bot_vs_bot(strategy_file1: str, strategy_file2: str, num_games: int = 100):
    """Have two bots play against each other"""
    # Load both strategies
    trainer1 = SimpleCFRTrainer()
    trainer2 = SimpleCFRTrainer()

    try:
        trainer1.load_strategy(strategy_file1)
        print(f"Loaded Bot 1 strategy from {strategy_file1}")
    except FileNotFoundError:
        print(f"Error: Strategy file {strategy_file1} not found!")
        return

    try:
        trainer2.load_strategy(strategy_file2)
        print(f"Loaded Bot 2 strategy from {strategy_file2}")
    except FileNotFoundError:
        print(f"Error: Strategy file {strategy_file2} not found!")
        return

    bot1 = CoupBot(trainer1)
    bot2 = CoupBot(trainer2)

    # Track statistics
    bot1_wins = 0
    bot2_wins = 0
    draws = 0

    print(f"\nPlaying {num_games} games...")

    for game_num in tqdm(range(num_games)):
        state = create_initial_state()

        # Play out the game
        while not state.is_terminal():
            current_player = state.current_player

            # Choose bot based on current player
            if current_player == 1:
                action, _, _ = bot1.choose_action(state, current_player)
            else:
                action, _, _ = bot2.choose_action(state, current_player)

            # Apply action
            state = apply_action(state, action)

        # Record result (from player 1's perspective)
        utility = state.get_utility(1)
        if utility > 0:
            bot1_wins += 1
        elif utility < 0:
            bot2_wins += 1
        else:
            draws += 1

    # Print results
    print(f"\n{'='*50}")
    print(f"Results after {num_games} games:")
    print(f"{'='*50}")
    print(f"Bot 1 ({strategy_file1}): {bot1_wins} wins ({bot1_wins/num_games*100:.1f}%)")
    print(f"Bot 2 ({strategy_file2}): {bot2_wins} wins ({bot2_wins/num_games*100:.1f}%)")
    print(f"Draws: {draws} ({draws/num_games*100:.1f}%)")
    print(f"{'='*50}")

if __name__ == "__main__":
    # Add this to your main file
    import sys
    import argparse
    parser = argparse.ArgumentParser(description='Play Coup')
    parser.add_argument('filename', type=str, help='First strategy JSON file')
    parser.add_argument('--bot-vs-bot', type=str, help='Second strategy JSON file for bot vs bot mode')
    parser.add_argument('--num-games', type=int, default=100, help='Number of games for bot vs bot mode')
    parser.add_argument('--depth_limit', type=int, default=20, help='Maximum game depth (must match C++ configuration)')
    parser.add_argument('--player', type=int, default=1, choices=[1,2], help='Which player you are (1 or 2) in GUI mode')
    args = parser.parse_args()

    # Update global DEPTH_LIMIT if specified
    DEPTH_LIMIT = args.depth_limit

    if args.bot_vs_bot:
        # Bot vs bot mode
        play_bot_vs_bot(args.filename, args.bot_vs_bot, num_games=args.num_games)
    else:
        # GUI mode (human vs bot)
        play_gui(args.filename, human_player=args.player)

