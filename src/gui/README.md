# Coup Game UI

Play Coup against a bot trained with CFR (Counterfactual Regret Minimization).

## Quick Start

### Option 1: Demo Mode (No Training Required)

Try the game immediately with a random bot:

```bash
python src/gui/coup_demo.py
```

This is perfect for:
- Learning how to play Coup
- Testing the UI
- Understanding the game mechanics

### Option 2: Play Against a Trained CFR Bot

1. **Train a bot** using the C++ CFR trainer:
   ```bash
   # From the project root
   ./coup_train --variant base --iterations 50000 --rollouts 24 --depth 16
   ```

   This will create a strategy file like: `base_strategy_16_24_50000.json`

2. **Play against the bot**:
   ```bash
   python src/gui/coup_ui.py base_strategy_16_24_50000.json
   ```

## Supported Variants

The UI automatically detects the game variant from the JSON filename:

### Base Coup (default)
- **Filename pattern**: `base_strategy_*.json`
- **Rules**: 2 influences per player, 3 card types (Duke, Captain, Assassin)
- **Actions**: Income, Tax, Steal, Assassinate, Coup

### Simple Coup
- **Filename pattern**: `simple_strategy_*.json`
- **Rules**: 1 influence per player, 3 card types
- **Actions**: Income, Tax, Steal, Coup (no Assassinate)
- **Notes**: Faster games, cheaper coup threshold (7 coins)

### Full Coup
- **Filename pattern**: `full_strategy_*.json`
- **Rules**: 2 influences per player, 5 card types (Duke, Captain, Assassin, Ambassador, Contessa)
- **Actions**: All actions including Exchange and Block

## How to Play

### Game Flow

1. The game starts with both players having 2 influences (or 1 for Simple variant) and 2 coins
2. On your turn, choose an action by entering its number
3. The bot will respond based on its trained strategy
4. The game continues until one player loses all influences

### Actions

- **Income** (+1 coin): Always safe, cannot be challenged
- **Tax** (+3 coins): Claims Duke, can be challenged
- **Steal** (+2 coins from opponent): Claims Captain, can be challenged
- **Assassinate** (-3 coins, opponent loses influence): Claims Assassin, can be challenged
- **Coup** (-7 coins, opponent loses influence): Cannot be challenged, mandatory at 10+ coins

### Challenges

When an opponent claims an influence (Duke, Captain, or Assassin), you can:
- **Pass**: Allow the action to proceed
- **Challenge**: Force them to prove they have the card
  - If they have it: You lose an influence, they shuffle it back and draw a new card
  - If they don't: They lose an influence, action fails

## Configuration

### Abstraction Mode

The UI uses the abstraction mode defined in `src/python/game_state.py` (line 19):

```python
ABSTRACTION_MODE = AbstractionMode.NONE
```

**IMPORTANT**: This must match the C++ training configuration!

- `NONE`: No abstraction (exact coin counts)
- `ASYMMETRIC`: Exact player coins, abstracted opponent coins
- `SYMMETRIC`: Both players' coins abstracted
- `FINE_GRAINED`: 5 buckets instead of 4

### Other Settings

All settings in `game_state.py` should match your C++ trainer:
- `DEPTH_LIMIT = 20`
- `ENABLE_EARLY_TERMINATION = True`
- `EARLY_TERM_MIN_DEPTH = 10`
- `EARLY_TERM_SCORE_THRESHOLD = 14`

## Display Legend

- 👤 **YOU** - Human player (Player 1)
- 🤖 **BOT** - CFR-trained bot (Player 2)
- 💰 Economic actions (Income, Tax, Steal)
- ⚔️ Aggressive actions (Assassinate, Coup, Challenge)
- 📋 Revealed cards (lost influences)
- 📚 Deck
- ⏳ Pending action (waiting for challenge response)

## Tips

1. **Pay attention to revealed cards**: If both copies of a card are revealed, you know the opponent is bluffing
2. **Track your opponent's coins**: At 10+ coins, they must coup
3. **Bluff strategically**: The bot may not always challenge
4. **Consider the bot's training**: Better-trained bots (more iterations) play stronger

## Troubleshooting

### "Strategy file not found"
- Make sure you've trained a bot first using the C++ trainer
- Check that the JSON file path is correct

### "Info set not in strategy"
- This warning appears when the game reaches a state not seen during training
- The bot will use a random action in this case
- Train with more iterations or higher depth to reduce this

### "Abstraction mode mismatch"
- Make sure `ABSTRACTION_MODE` in `game_state.py` matches your C++ configuration
- The bot's strategy is trained for a specific abstraction mode

### "Invalid JSON"
- Check that the strategy file is complete and not corrupted
- Re-run the trainer if needed

## Example Session

```
$ python src/gui/coup_ui.py base_strategy_16_24_50000.json

============================================================
🎲 COUP - Play Against CFR Bot
============================================================
✓ Loaded strategy with 45231 information sets
✓ Detected variant: Base Coup (2 influences, 3 card types)

🎴 Initial cards dealt!
   Your influences: DUKE, CAPTAIN

Press Enter to start the game...

============================================================
TURN 1
============================================================

============================================================
GAME STATE
============================================================

👤 YOU (Player 1):
  Coins: 2
  Influences: DUKE, CAPTAIN

🤖 BOT (Player 2):
  Coins: 2
  Influences: 2 card(s) remaining

🎯 Current turn: YOU
============================================================

🎮 Your available actions:
  1. INCOME (+1 coin)
  2. TAX (+3 coins, claims Duke)
  3. STEAL (+2 coins from opponent, claims Captain)

Enter action number: 2

💰 YOU uses TAX
   (Claims to have DUKE)

Press Enter to continue...
```

## Advanced: Creating Custom Variants

To create a new variant:

1. Define rules in `src/cpp/rules/your_variant_rules.h`
2. Add it to `src/cpp/rules/game_rules.h`
3. Update `src/cpp/main.cpp` to support the variant
4. Train the bot with your variant
5. The UI will work automatically if the filename contains your variant name

## Credits

This UI is designed to work with the CFR-based Coup solver. It uses the game logic from `src/python/game_state.py` which mirrors the C++ implementation.
