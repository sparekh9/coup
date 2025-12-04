"""
Coup GUI Package

Play Coup variants against trained CFR bots.
"""

from .game_state import GameState, Action, Influence, ChallengeResponse, BlockResponse
from .rules import CoupRules, SimpleCoupRules, SimpleCoupBlockingRules, BaseCoupRules, get_rules
from .bot_player import BotPlayer
from .game_engine import GameEngine

__all__ = [
    'GameState',
    'Action',
    'Influence',
    'ChallengeResponse',
    'BlockResponse',
    'CoupRules',
    'SimpleCoupRules',
    'SimpleCoupBlockingRules',
    'BaseCoupRules',
    'get_rules',
    'BotPlayer',
    'GameEngine',
]
