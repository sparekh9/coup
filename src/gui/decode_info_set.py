#!/usr/bin/env python3
"""
Information Set Key Decoder

Decodes hex information set keys back into human-readable game state components.
Supports all three variants: Simple, Base, and Full Coup.

Usage:
    python decode_info_set.py <hex_key> <variant>

Example:
    python decode_info_set.py 0x1a2b3c simple
"""

import sys
from pathlib import Path
from typing import Dict, Any

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent))

import game_state
from game_state import (
    initialize_variant,
    AbstractionMode, ABSTRACTION_MODE
)


class InfoSetDecoder:
    """Decodes information set keys into readable game state"""

    def __init__(self, variant: str):
        """Initialize decoder for specific variant"""
        initialize_variant(variant)
        self.variant = variant
        # Access CURRENT_VARIANT through module to get updated reference
        self.config = game_state.CURRENT_VARIANT

    def decode(self, hex_key: str) -> Dict[str, Any]:
        """
        Decode a hex information set key using FIXED-WIDTH layout.

        Returns a dictionary with all decoded fields.
        """
        # Convert hex string to integer
        if hex_key.startswith('0x'):
            hash_val = int(hex_key, 16)
        else:
            hash_val = int('0x' + hex_key, 16)

        result = {}
        result['variant'] = self.variant
        result['max_influences'] = self.config.MAX_INFLUENCES_PER_PLAYER

        # Decode in REVERSE order (last encoded = first decoded)
        # Each field has FIXED WIDTH - no variable-length complexity!

        # 1. Opponent's last claim (3 bits)
        result['opp_last_claim'] = self._decode_claim(hash_val & 0b111)
        hash_val >>= 3

        # 2. My last claim (3 bits)
        result['my_last_claim'] = self._decode_claim(hash_val & 0b111)
        hash_val >>= 3

        # 3. Pending action (ALWAYS 3 bits, 7 = none)
        pending_value = hash_val & 0b111
        hash_val >>= 3
        if pending_value == 7:
            result['pending_action'] = None
            result['has_pending_action'] = False
        else:
            result['pending_action'] = self._decode_action(pending_value)
            result['has_pending_action'] = True

        # 4. Revealed cards (ALWAYS 4 slots of 3 bits, 7 = empty)
        revealed_cards_bits = []
        for i in range(4):
            card_bits = hash_val & 0b111
            hash_val >>= 3
            if card_bits != 7:  # Skip empty slots
                revealed_cards_bits.append(card_bits)

        # Cards were extracted in reverse order, so reverse them back
        result['revealed_cards'] = [self._decode_influence(c) for c in reversed(revealed_cards_bits)]
        result['revealed_count'] = len(revealed_cards_bits)

        # 5. Opponent influence count (2 bits)
        result['opp_influence_count'] = hash_val & 0b11
        hash_val >>= 2

        # 6. Opponent coins (ALWAYS 5 bits, exact or abstracted value)
        result['opp_coins'] = hash_val & 0b11111
        result['opp_coins_abstracted'] = (ABSTRACTION_MODE != AbstractionMode.NONE)
        hash_val >>= 5

        # 7. My coins (ALWAYS 5 bits, exact or abstracted value)
        result['my_coins'] = hash_val & 0b11111
        result['my_coins_abstracted'] = (ABSTRACTION_MODE != AbstractionMode.NONE)
        hash_val >>= 5

        # 8. My influences (ALWAYS 2 slots of 4 bits, 7 = empty)
        influences = []
        for i in range(2):
            inf_bits = hash_val & 0b1111
            hash_val >>= 4
            if inf_bits != 7:  # Skip empty slots
                influences.append(self._decode_influence(inf_bits))

        result['my_influences'] = list(reversed(influences))  # Reverse to get correct order

        # 9. Start bit (should be 1)
        result['start_bit'] = hash_val & 0b1
        hash_val >>= 1

        # Check if we've consumed all bits (hash_val should be 0 now)
        result['remaining_bits'] = hash_val
        result['fully_decoded'] = (hash_val == 0)

        return result

    def _decode_influence(self, value: int) -> str:
        """Decode influence value to name"""
        if value >= self.config.NUM_INFLUENCE_TYPES:
            return f"UNKNOWN({value})"

        # Find the enum member with this value
        for member in self.config.Influence:
            if member.value == value:
                return member.name

        return f"UNKNOWN({value})"

    def _decode_action(self, value: int) -> str:
        """Decode action value to name"""
        actions = list(self.config.Action)
        if value >= len(actions):
            return f"UNKNOWN_ACTION({value})"
        return actions[value].name

    def _decode_claim(self, value: int) -> str:
        """Decode claim value to influence name or None"""
        if value == 7:
            return "NO_CLAIM"
        return self._decode_influence(value)

    def format_decoded(self, decoded: Dict[str, Any]) -> str:
        """Format decoded information set as human-readable string"""
        lines = []
        lines.append(f"=== Information Set Decoded ({self.variant.upper()} variant) ===")
        lines.append(f"Config: MAX_INFLUENCES={decoded.get('max_influences', 'unknown')}")
        lines.append("")

        # Player's perspective
        lines.append("MY STATE:")
        lines.append(f"  Influences: {decoded['my_influences']} ({len(decoded['my_influences'])} cards)")
        coins_str = f"{decoded['my_coins']}"
        if decoded['my_coins_abstracted']:
            coins_str += " (abstracted bucket)"
        lines.append(f"  Coins: {coins_str}")
        lines.append(f"  Last claim: {decoded['my_last_claim']}")

        lines.append("")
        lines.append("OPPONENT STATE:")
        lines.append(f"  Influence count: {decoded['opp_influence_count']}")
        opp_coins_str = f"{decoded['opp_coins']}"
        if decoded['opp_coins_abstracted']:
            opp_coins_str += " (abstracted bucket)"
        lines.append(f"  Coins: {opp_coins_str}")
        lines.append(f"  Last claim: {decoded['opp_last_claim']}")

        lines.append("")
        lines.append("PUBLIC INFORMATION:")
        lines.append(f"  Revealed cards: {decoded['revealed_cards']} ({decoded['revealed_count']} total)")

        if decoded['has_pending_action']:
            lines.append(f"  Pending action: {decoded['pending_action']} (waiting for response)")
        else:
            lines.append("  Pending action: None")

        lines.append("")
        lines.append("DECODER INFO:")
        lines.append(f"  Start bit: {decoded['start_bit']} (should be 1)")
        lines.append(f"  Fully decoded: {decoded['fully_decoded']}")
        if not decoded['fully_decoded']:
            lines.append(f"  WARNING: Remaining bits: {decoded['remaining_bits']:x}")

        return "\n".join(lines)


def decode_abstraction_bucket(coins: int, is_my_coins: bool = True) -> str:
    """Decode coin abstraction bucket to explanation"""
    if ABSTRACTION_MODE == AbstractionMode.NONE:
        return f"{coins} coins (exact)"

    if is_my_coins and ABSTRACTION_MODE == AbstractionMode.ASYMMETRIC:
        return f"{coins} coins (exact, asymmetric mode)"

    # For abstracted buckets
    if is_my_coins or ABSTRACTION_MODE == AbstractionMode.SYMMETRIC:
        # 4 buckets
        if coins == 0:
            return "0-2 coins (can't assassinate)"
        elif coins == 1:
            return "3-6 coins (can assassinate, can't coup)"
        elif coins == 2:
            return "7-9 coins (can coup, optional)"
        else:  # 3
            return "10+ coins (must coup)"
    else:
        # Opponent abstraction (3 buckets)
        if coins == 0:
            return "0-6 coins (can't coup me)"
        elif coins == 1:
            return "7-9 coins (can coup me, optional)"
        else:  # 2
            return "10+ coins (must coup me)"


def main():
    """Command-line interface"""
    if len(sys.argv) < 3:
        print("Usage: python decode_info_set.py <hex_key> <variant>")
        print("\nExample:")
        print("  python decode_info_set.py 0x1a2b3c base")
        print("\nVariants: simple, base, full")
        sys.exit(1)

    hex_key = sys.argv[1]
    variant = sys.argv[2].lower()

    if variant not in ['simple', 'base', 'full']:
        print(f"Error: Unknown variant '{variant}'")
        print("Valid variants: simple, base, full")
        sys.exit(1)

    try:
        decoder = InfoSetDecoder(variant)
        decoded = decoder.decode(hex_key)
        print(decoder.format_decoded(decoded))

        # Show abstraction details if applicable
        if ABSTRACTION_MODE != AbstractionMode.NONE:
            print("\n=== Abstraction Details ===")
            print(f"Mode: {ABSTRACTION_MODE.name}")
            my_coins_desc = decode_abstraction_bucket(decoded['my_coins'], is_my_coins=True)
            opp_coins_desc = decode_abstraction_bucket(decoded['opp_coins'], is_my_coins=False)
            print(f"My coins: {my_coins_desc}")
            print(f"Opponent coins: {opp_coins_desc}")

    except Exception as e:
        print(f"Error decoding key: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()
