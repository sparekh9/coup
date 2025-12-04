# Coup GUI - Play Against Trained Bots

A graphical interface for playing Coup variants against CFR-trained bot strategies.

## Features

- **Three Variants Supported:**
  - **Simple**: 1 influence per player, 3 card types (Duke, Captain, Assassin), no blocking
  - **Simple Blocking**: 1 influence per player, 4 card types (+ Contessa), with blocking mechanics
  - **Base**: 2 influences per player, 3 card types (Duke, Captain, Assassin), no blocking

- **Play Against Trained Bots**: Load JSON strategy files from CFR training
- **Visual Interface**: Clean Tkinter-based GUI showing game state, cards, and actions
- **Game Log**: Track the full history of the game
- **Modular Design**: Easy to extend for bot-vs-bot simulation

## Installation

### Requirements

- Python 3.8+
- tkinter (usually included with Python)

No additional dependencies required!

## Usage

### Running the GUI

From the `src/python/gui` directory:

```bash
python coup_gui.py
```

Or use the launcher script from the `src` directory:

```bash
python -m python.gui.coup_gui
```

### Playing a Game

1. **Select Variant**: Choose which Coup variant to play
2. **Load Strategy**: Browse for a JSON strategy file (e.g., `simple_strategy_24_200.json`)
3. **Start Game**: Click "Start Game" to begin
4. **Play**: Click action buttons on your turn, bot will respond automatically

### Strategy Files

Strategy files are JSON files output by the C++ CFR trainer. Example locations:
- `../../simple_strategy_24_200.json`
- `../../base_strategy_16_1.json`
- `../../simpleassassin_strategy_20_1000.json` (for simpleblocking variant)

## Architecture

The GUI is built with a modular architecture:

```
game_state.py       - Game state representation
rules.py            - Variant-specific rules (simple, simpleblocking, base)
bot_player.py       - Bot that loads and uses JSON strategies
game_engine.py      - Core game logic and action resolution
coup_gui.py         - Tkinter GUI interface
```

### Key Components

#### GameState
Represents the current game state including:
- Player influences (hidden cards)
- Coins
- Revealed cards
- Pending actions (challenges, blocks)
- Claim history

#### Rules Classes
Encapsulate variant-specific rules:
- Available cards and actions
- Action costs and effects
- Challenge/block mechanics
- Force challenge/block logic

#### BotPlayer
- Loads trained strategy from JSON
- Computes info set keys (matching C++ implementation)
- Samples actions according to strategy probabilities

#### GameEngine
- Manages game flow
- Applies actions and transitions state
- Handles challenges, blocks, and influence loss
- Enforces game rules

#### CoupGUI
- Tkinter-based visual interface
- Shows player cards, coins, and game state
- Provides action buttons
- Displays game log

## Info Set Key Computation

The bot computes info set keys using the exact same bit-packing scheme as the C++ implementation:

- Player influences (sorted, 2 slots × 3 bits)
- My coins (4 bits)
- Opponent coins (4 bits)
- Opponent influence count (2 bits)
- Revealed cards (sorted, 4 slots × 3 bits)
- Pending state (5 bits: state type + action/claim)
- Claim history (2 × 3 slots × 3 bits)
- Claim counts (2 × 2 bits)

This ensures the bot strategy is used correctly.

## Extending for Bot vs Bot

The modular design makes it easy to add bot-vs-bot simulation:

```python
from game_engine import GameEngine
from bot_player import BotPlayer

# Create two bots
engine = GameEngine("simple")
bot1 = BotPlayer("strategy1.json", "simple", 1)
bot2 = BotPlayer("strategy2.json", "simple", 2)

# Run game
state = engine.create_initial_state()
while not engine.is_game_over(state):
    if state.current_player == 1:
        action = bot1.get_action(state)
    else:
        action = bot2.get_action(state)
    state = engine.apply_action(state, action)

winner = engine.get_winner(state)
print(f"Winner: Player {winner}")
```

See `simulate_games.py` for a full bot-vs-bot simulation implementation.

## Game Flow

### Normal Action Flow
1. Player selects action (INCOME, TAX, STEAL, ASSASSINATE, COUP)
2. If challengeable, opponent can PASS or CHALLENGE
3. If challenged:
   - Has card: Challenger loses influence, action succeeds
   - No card: Actor loses influence, action fails
4. If blockable (and not challenged), opponent can PASS or BLOCK
5. If blocked, actor can PASS or CHALLENGE the block
6. Action executes (if not blocked/challenged successfully)

### Actions

- **INCOME**: Gain 1 coin (no challenge)
- **TAX**: Gain 3 coins (requires Duke, challengeable)
- **STEAL**: Steal 2 coins (requires Captain, challengeable, blockable by Captain)
- **ASSASSINATE**: Pay 3 coins, target loses influence (requires Assassin, challengeable, blockable by Contessa)
- **COUP**: Pay 7 coins, target loses influence (no challenge)

## Troubleshooting

### Strategy file not loading
- Ensure the JSON file is a valid CFR strategy output
- Check that the variant matches the strategy file

### Bot making random moves
- If an info set is not found in the strategy, the bot will use uniform random
- This can happen if the game reaches a state not seen during training

### GUI not responding
- The bot delays moves by 500ms for visibility
- If game hangs, check terminal for error messages

## Future Enhancements

- [ ] Bot-vs-bot simulation mode
- [ ] Strategy comparison tool
- [ ] Replay saved games
- [ ] Adjustable bot thinking time
- [ ] Sound effects
- [ ] Better card visualization (images)
- [ ] Support for more variants (FullCoup, custom variants)

## License

Part of the Computational Game Solving course project (15-888).
