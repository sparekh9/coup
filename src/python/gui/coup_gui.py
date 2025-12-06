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
        self.strategy_file: Optional[str] = None
        self.human_player_id = 1  # Will alternate between 1 and 2
        self.bot_player_id = 2

        # UI settings
        self.show_bot_info = tk.BooleanVar(value=True)  # Toggle for bot info set display

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
            self.strategy_file = strategy_file
            self.human_player_id = 1  # Reset to player 1 for first game
            self.bot_player_id = 2
            self.engine = GameEngine(variant)
            self.state = self.engine.create_initial_state()
            self.bot = BotPlayer(strategy_file, variant, self.bot_player_id)
            self.game_log = []

            # Show game screen
            self._show_game_screen()

            # Log game start
            self._log(f"=== Starting {variant.upper()} Coup Game ===")
            self._log(f"You are Player {self.human_player_id}, Bot is Player {self.bot_player_id}")
            human_influences = self.state.p1_influences if self.human_player_id == 1 else self.state.p2_influences
            self._log(f"Your cards: {[inf.name for inf in human_influences]}")
            self._log("")

            # Update display
            self._update_display()

        except Exception as e:
            messagebox.showerror("Error", f"Failed to start game: {e}")
            import traceback
            traceback.print_exc()

    def _restart_game(self):
        """Restart game with same settings but swapped players."""
        if not self.variant or not self.strategy_file:
            messagebox.showerror("Error", "No active game to restart")
            return

        try:
            # Swap player IDs
            self.human_player_id, self.bot_player_id = self.bot_player_id, self.human_player_id

            # Initialize new game
            self.engine = GameEngine(self.variant)
            self.state = self.engine.create_initial_state()
            self.bot = BotPlayer(self.strategy_file, self.variant, self.bot_player_id)
            self.game_log = []

            # Clear and recreate game screen
            self._show_game_screen()

            # Log game start
            self._log(f"=== Starting {self.variant.upper()} Coup Game ===")
            self._log(f"You are Player {self.human_player_id}, Bot is Player {self.bot_player_id}")
            human_influences = self.state.p1_influences if self.human_player_id == 1 else self.state.p2_influences
            self._log(f"Your cards: {[inf.name for inf in human_influences]}")
            self._log("")

            # Update display
            self._update_display()

        except Exception as e:
            messagebox.showerror("Error", f"Failed to restart game: {e}")
            import traceback
            traceback.print_exc()

    def _show_game_over_dialog(self, message: str):
        """Show game over dialog with options to play again or change settings."""
        # Create custom dialog
        dialog = tk.Toplevel(self.root)
        dialog.title("Game Over")
        dialog.geometry("400x200")
        dialog.transient(self.root)
        dialog.grab_set()

        # Center the dialog
        dialog.update_idletasks()
        x = (dialog.winfo_screenwidth() // 2) - (dialog.winfo_width() // 2)
        y = (dialog.winfo_screenheight() // 2) - (dialog.winfo_height() // 2)
        dialog.geometry(f"+{x}+{y}")

        # Message
        msg_frame = ttk.Frame(dialog, padding="20")
        msg_frame.pack(fill=tk.BOTH, expand=True)

        ttk.Label(msg_frame, text=message, font=("Arial", 14, "bold")).pack(pady=(10, 20))
        ttk.Label(msg_frame, text="What would you like to do?", font=("Arial", 11)).pack(pady=(0, 20))

        # Buttons
        button_frame = ttk.Frame(msg_frame)
        button_frame.pack()

        def new_game():
            dialog.destroy()
            self._restart_game()

        def change_settings():
            dialog.destroy()
            self._show_setup_screen()

        ttk.Button(button_frame, text="New Game (Swap Players)", command=new_game, width=25).pack(side=tk.LEFT, padx=5)
        ttk.Button(button_frame, text="Change Settings", command=change_settings, width=25).pack(side=tk.LEFT, padx=5)

        # Make dialog modal
        self.root.wait_window(dialog)

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
        bot_frame = ttk.LabelFrame(game_frame, text=f"Bot (Player {self.bot_player_id})", padding="10")
        bot_frame.grid(row=0, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=5)
        self.bot_info_label = ttk.Label(bot_frame, text="", font=("Arial", 11))
        self.bot_info_label.pack()

        # Middle: Game state
        state_frame = ttk.LabelFrame(game_frame, text="Game State", padding="10")
        state_frame.grid(row=1, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=5)
        self.state_label = ttk.Label(state_frame, text="", font=("Arial", 10))
        self.state_label.pack()

        # Player info
        player_frame = ttk.LabelFrame(game_frame, text=f"You (Player {self.human_player_id})", padding="10")
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

        # Toggle for bot info
        log_toggle_frame = ttk.Frame(log_frame)
        log_toggle_frame.pack(fill=tk.X, pady=(0, 5))
        ttk.Checkbutton(
            log_toggle_frame,
            text="Show Bot's Information Set",
            variable=self.show_bot_info
        ).pack(anchor=tk.W)

        # Scrollable text widget
        log_scroll = ttk.Scrollbar(log_frame)
        log_scroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.log_text = tk.Text(log_frame, width=40, height=20, yscrollcommand=log_scroll.set, state=tk.DISABLED)
        self.log_text.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        log_scroll.config(command=self.log_text.yview)

        # New Game button
        button_frame = ttk.Frame(game_frame)
        button_frame.grid(row=4, column=0, columnspan=2, pady=10)
        ttk.Button(button_frame, text="New Game (Swap Players)", command=self._restart_game).pack()

        # Configure grid weights
        game_frame.columnconfigure(0, weight=1)
        game_frame.columnconfigure(1, weight=1)
        game_frame.rowconfigure(3, weight=1)

    def _update_display(self):
        """Update all display elements."""
        if not self.state:
            return

        # Update bot info based on bot_player_id
        if self.bot_player_id == 1:
            bot_coins = self.state.p1_coins
            bot_influences = len(self.state.p1_influences)
        else:
            bot_coins = self.state.p2_coins
            bot_influences = len(self.state.p2_influences)
        self.bot_info_label.config(text=f"Coins: {bot_coins} | Influences: {bot_influences}")

        # Update player info based on human_player_id
        if self.human_player_id == 1:
            player_coins = self.state.p1_coins
            player_influences = [inf.name for inf in self.state.p1_influences]
        else:
            player_coins = self.state.p2_coins
            player_influences = [inf.name for inf in self.state.p2_influences]
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
                win_message = "Congratulations! You won!"
            else:
                self._log("\n=== BOT WINS! ===")
                win_message = "Bot wins! Better luck next time."
            self._show_game_over_dialog(win_message)
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

        # Get legal actions (no pruning for human player)
        legal_actions = self.engine.get_legal_actions(self.state, apply_pruning=False)

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

        # Log bot's information set (what the bot can see) if enabled
        if self.show_bot_info.get():
            self._log_bot_info_set()

        # Get bot action
        action = self.bot.get_action(self.state)

        # Log action
        action_name = self._get_action_display_name(action)
        self._log(f"Bot: {action_name}")

        # Apply action
        self.state = self.engine.apply_action(self.state, action)

        # Update display
        self._update_display()

    def _log_bot_info_set(self):
        """Log detailed information set that the bot sees."""
        # Get bot's perspective on the game state
        bot_id = self.bot_player_id
        human_id = self.human_player_id

        # Bot's visible information
        bot_influences = sorted([inf.name for inf in self.state.get_player_influences(bot_id)])
        bot_coins = self.state.get_player_coins(bot_id)

        # Opponent information (what bot knows about human)
        human_inf_count = len(self.state.get_player_influences(human_id))
        human_coins = self.state.get_player_coins(human_id)

        # Revealed cards
        revealed = sorted([card.name for card in self.state.revealed_cards])

        # Pending state
        pending_info = "None"
        if self.state.has_pending_action:
            pending_info = f"Action: {self.state.pending_action_type.name} by Player {self.state.pending_action_actor}"
        elif self.state.has_pending_block_challenge:
            pending_info = f"Block Challenge: {self.state.pending_block_claim.name} claim"

        # Claim histories (circular buffers)
        bot_claims = [self._influence_value_to_name(val) for val in
                      (self.state.p2_claim_history if bot_id == 2 else self.state.p1_claim_history)]
        human_claims = [self._influence_value_to_name(val) for val in
                        (self.state.p1_claim_history if bot_id == 2 else self.state.p2_claim_history)]

        bot_claim_count = self.state.p2_claim_count if bot_id == 2 else self.state.p1_claim_count
        human_claim_count = self.state.p1_claim_count if bot_id == 2 else self.state.p2_claim_count

        # Log the information set
        self._log("--- Bot's Information Set ---")
        self._log(f"  Bot's Cards: {bot_influences}")
        self._log(f"  Bot's Coins: {bot_coins}")
        self._log(f"  Opponent Influence Count: {human_inf_count}")
        self._log(f"  Opponent Coins: {human_coins}")
        self._log(f"  Revealed Cards: {revealed}")
        self._log(f"  Pending State: {pending_info}")
        self._log(f"  Bot's Claim History: {bot_claims} (count: {bot_claim_count})")
        self._log(f"  Opponent Claim History: {human_claims} (count: {human_claim_count})")
        self._log("-----------------------------")

    def _influence_value_to_name(self, value: int) -> str:
        """Convert influence value to name (7 = empty slot)."""
        if value == 7:
            return "EMPTY"
        elif value == 0:
            return "DUKE"
        elif value == 1:
            return "CAPTAIN"
        elif value == 2:
            return "ASSASSIN"
        elif value == 3:
            return "CONTESSA"
        else:
            return f"UNKNOWN({value})"

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
