"""
Tkinter GUI for playing Coup against trained bots.

This module provides a graphical interface for playing Coup variants
against CFR-trained bot strategies.
"""

import tkinter as tk
from tkinter import ttk, messagebox, filedialog
from typing import Optional, Union
import sys

from game_state import GameState, Action, Influence, ChallengeResponse
from game_engine import GameEngine
from bot_player import BotPlayer
from rules import get_rules


class CoupGUI:
    """Main GUI for Coup game."""

    def __init__(self, root):
        """Initialize the GUI."""
        self.root = root
        self.root.title("Coup - Play vs Bot")
        self.root.geometry("900x700")

        # Game state
        self.engine: Optional[GameEngine] = None
        self.state: Optional[GameState] = None
        self.bot: Optional[BotPlayer] = None
        self.variant: Optional[str] = None
        self.human_player_id = 1  # Human is always player 1
        self.bot_player_id = 2

        # Game log
        self.game_log = []

        # Create UI
        self._create_menu()
        self._create_main_layout()

        # Show setup screen
        self._show_setup_screen()

    def _create_menu(self):
        """Create menu bar."""
        menubar = tk.Menu(self.root)
        self.root.config(menu=menubar)

        # Game menu
        game_menu = tk.Menu(menubar, tearoff=0)
        menubar.add_cascade(label="Game", menu=game_menu)
        game_menu.add_command(label="New Game", command=self._show_setup_screen)
        game_menu.add_separator()
        game_menu.add_command(label="Exit", command=self.root.quit)

    def _create_main_layout(self):
        """Create main layout."""
        # Main container
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)

        # Content frame (will show setup or game)
        self.content_frame = ttk.Frame(main_frame)
        self.content_frame.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        main_frame.columnconfigure(0, weight=1)
        main_frame.rowconfigure(0, weight=1)

    def _show_setup_screen(self):
        """Show game setup screen."""
        # Clear content
        for widget in self.content_frame.winfo_children():
            widget.destroy()

        setup_frame = ttk.Frame(self.content_frame, padding="20")
        setup_frame.grid(row=0, column=0)

        # Title
        title = ttk.Label(setup_frame, text="Coup - Game Setup", font=("Arial", 20, "bold"))
        title.grid(row=0, column=0, columnspan=2, pady=20)

        # Variant selection
        ttk.Label(setup_frame, text="Select Variant:", font=("Arial", 12)).grid(row=1, column=0, sticky=tk.W, pady=10)
        self.variant_var = tk.StringVar(value="simple")
        variant_frame = ttk.Frame(setup_frame)
        variant_frame.grid(row=1, column=1, sticky=tk.W, pady=10)
        ttk.Radiobutton(variant_frame, text="Simple (1 influence, 3 cards)", variable=self.variant_var, value="simple").pack(anchor=tk.W)
        ttk.Radiobutton(variant_frame, text="Simple Blocking (1 influence, 4 cards, blocking)", variable=self.variant_var, value="simpleblocking").pack(anchor=tk.W)
        ttk.Radiobutton(variant_frame, text="Base (2 influences, 3 cards)", variable=self.variant_var, value="base").pack(anchor=tk.W)

        # Strategy file selection
        ttk.Label(setup_frame, text="Bot Strategy File:", font=("Arial", 12)).grid(row=2, column=0, sticky=tk.W, pady=10)
        self.strategy_file_var = tk.StringVar(value="")
        strategy_frame = ttk.Frame(setup_frame)
        strategy_frame.grid(row=2, column=1, sticky=(tk.W, tk.E), pady=10)
        ttk.Entry(strategy_frame, textvariable=self.strategy_file_var, width=40).pack(side=tk.LEFT, padx=(0, 5))
        ttk.Button(strategy_frame, text="Browse...", command=self._browse_strategy_file).pack(side=tk.LEFT)

        # Start button
        ttk.Button(setup_frame, text="Start Game", command=self._start_game).grid(row=3, column=0, columnspan=2, pady=20)

    def _browse_strategy_file(self):
        """Browse for strategy JSON file."""
        filename = filedialog.askopenfilename(
            title="Select Strategy File",
            filetypes=[("JSON files", "*.json"), ("All files", "*.*")],
            initialdir="../.."
        )
        if filename:
            self.strategy_file_var.set(filename)

    def _start_game(self):
        """Start a new game."""
        variant = self.variant_var.get()
        strategy_file = self.strategy_file_var.get()

        if not strategy_file:
            messagebox.showerror("Error", "Please select a strategy file")
            return

        try:
            # Initialize game
            self.variant = variant
            self.engine = GameEngine(variant)
            self.state = self.engine.create_initial_state()
            self.bot = BotPlayer(strategy_file, variant, self.bot_player_id)
            self.game_log = []

            # Show game screen
            self._show_game_screen()

            # Log game start
            self._log(f"=== Starting {variant.upper()} Coup Game ===")
            self._log(f"You are Player 1, Bot is Player 2")
            self._log(f"Your cards: {[inf.name for inf in self.state.p1_influences]}")
            self._log("")

            # Update display
            self._update_display()

        except Exception as e:
            messagebox.showerror("Error", f"Failed to start game: {e}")
            import traceback
            traceback.print_exc()

    def _show_game_screen(self):
        """Show main game screen."""
        # Clear content
        for widget in self.content_frame.winfo_children():
            widget.destroy()

        # Main game layout
        game_frame = ttk.Frame(self.content_frame)
        game_frame.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        self.content_frame.columnconfigure(0, weight=1)
        self.content_frame.rowconfigure(0, weight=1)

        # Top: Bot info
        bot_frame = ttk.LabelFrame(game_frame, text="Bot (Player 2)", padding="10")
        bot_frame.grid(row=0, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=5)
        self.bot_info_label = ttk.Label(bot_frame, text="", font=("Arial", 11))
        self.bot_info_label.pack()

        # Middle: Game state
        state_frame = ttk.LabelFrame(game_frame, text="Game State", padding="10")
        state_frame.grid(row=1, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=5)
        self.state_label = ttk.Label(state_frame, text="", font=("Arial", 10))
        self.state_label.pack()

        # Player info
        player_frame = ttk.LabelFrame(game_frame, text="You (Player 1)", padding="10")
        player_frame.grid(row=2, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=5)
        self.player_info_label = ttk.Label(player_frame, text="", font=("Arial", 11))
        self.player_info_label.pack()

        # Actions
        actions_frame = ttk.LabelFrame(game_frame, text="Your Actions", padding="10")
        actions_frame.grid(row=3, column=0, sticky=(tk.W, tk.E, tk.N, tk.S), pady=5, padx=(0, 5))
        self.actions_frame = ttk.Frame(actions_frame)
        self.actions_frame.pack(fill=tk.BOTH, expand=True)

        # Game log
        log_frame = ttk.LabelFrame(game_frame, text="Game Log", padding="5")
        log_frame.grid(row=3, column=1, sticky=(tk.W, tk.E, tk.N, tk.S), pady=5, padx=(5, 0))

        # Scrollable text widget
        log_scroll = ttk.Scrollbar(log_frame)
        log_scroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.log_text = tk.Text(log_frame, width=40, height=20, yscrollcommand=log_scroll.set, state=tk.DISABLED)
        self.log_text.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        log_scroll.config(command=self.log_text.yview)

        # Configure grid weights
        game_frame.columnconfigure(0, weight=1)
        game_frame.columnconfigure(1, weight=1)
        game_frame.rowconfigure(3, weight=1)

    def _update_display(self):
        """Update all display elements."""
        if not self.state:
            return

        # Update bot info
        bot_coins = self.state.p2_coins
        bot_influences = len(self.state.p2_influences)
        self.bot_info_label.config(text=f"Coins: {bot_coins} | Influences: {bot_influences}")

        # Update player info
        player_coins = self.state.p1_coins
        player_influences = [inf.name for inf in self.state.p1_influences]
        self.player_info_label.config(text=f"Coins: {player_coins} | Your Cards: {player_influences}")

        # Update state info
        state_text = f"Current Turn: Player {self.state.current_player}"
        if self.state.revealed_cards:
            state_text += f" | Revealed: {[c.name for c in self.state.revealed_cards]}"
        if self.state.has_pending_action:
            state_text += f"\nPending: Player {self.state.pending_action_actor} played {self.state.pending_action_type.name}"
        elif self.state.has_pending_block_challenge:
            state_text += f"\nPending: Challenge block claim ({self.state.pending_block_claim.name})"
        self.state_label.config(text=state_text)

        # Update actions
        self._update_actions()

        # Check if game over
        if self.engine.is_game_over(self.state):
            winner = self.engine.get_winner(self.state)
            if winner == self.human_player_id:
                self._log("\n=== YOU WIN! ===")
                messagebox.showinfo("Game Over", "Congratulations! You won!")
            else:
                self._log("\n=== BOT WINS! ===")
                messagebox.showinfo("Game Over", "Bot wins! Better luck next time.")
            self._show_setup_screen()
            return

        # If bot's turn, make bot move
        if self.state.current_player == self.bot_player_id:
            self.root.after(500, self._bot_move)

    def _update_actions(self):
        """Update available action buttons."""
        # Clear existing buttons
        for widget in self.actions_frame.winfo_children():
            widget.destroy()

        # Don't show actions if it's bot's turn
        if self.state.current_player != self.human_player_id:
            ttk.Label(self.actions_frame, text="(Bot is thinking...)", font=("Arial", 10, "italic")).pack()
            return

        # Get legal actions
        legal_actions = self.engine.get_legal_actions(self.state)

        # Create buttons
        for i, action in enumerate(legal_actions):
            action_name = self._get_action_display_name(action)
            btn = ttk.Button(
                self.actions_frame,
                text=action_name,
                command=lambda a=action: self._human_action(a)
            )
            btn.pack(pady=2, fill=tk.X)

    def _get_action_display_name(self, action: Union[Action, ChallengeResponse]) -> str:
        """Get display name for an action."""
        if isinstance(action, Action):
            cost = self.engine.rules.get_action_cost(action)
            if cost > 0:
                return f"{action.name} (costs {cost} coins)"
            return action.name
        elif isinstance(action, ChallengeResponse):
            if action == ChallengeResponse.PASS:
                return "PASS"
            elif action == ChallengeResponse.CHALLENGE:
                return "CHALLENGE"
            elif action == ChallengeResponse.BLOCK:
                return "BLOCK"
        return str(action)

    def _human_action(self, action: Union[Action, ChallengeResponse]):
        """Handle human player action."""
        # Log action
        action_name = self._get_action_display_name(action)
        self._log(f"You: {action_name}")

        # Apply action
        self.state = self.engine.apply_action(self.state, action)

        # Update display
        self._update_display()

    def _bot_move(self):
        """Make bot move."""
        if not self.state or self.engine.is_game_over(self.state):
            return

        # Get bot action
        action = self.bot.get_action(self.state)

        # Log action
        action_name = self._get_action_display_name(action)
        self._log(f"Bot: {action_name}")

        # Apply action
        self.state = self.engine.apply_action(self.state, action)

        # Update display
        self._update_display()

    def _log(self, message: str):
        """Add message to game log."""
        self.game_log.append(message)
        self.log_text.config(state=tk.NORMAL)
        self.log_text.insert(tk.END, message + "\n")
        self.log_text.see(tk.END)
        self.log_text.config(state=tk.DISABLED)


def main():
    """Main entry point."""
    root = tk.Tk()
    app = CoupGUI(root)
    root.mainloop()


if __name__ == "__main__":
    main()
