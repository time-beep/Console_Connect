# Supabase Backend — Project Record

Supabase provides the PostgreSQL database and Edge Function API used by the Console Connect prototype.

## Components documented here

- `functions/console-api/index.ts` — reference implementation of the API contract.
- `migrations/001_console_schema.sql` — reference schema for `console_users` and `high_scores`.

## Recorded data model

- `console_users`: `id`, unique `user_id`, `password`.
- `high_scores`: `id`, `user_id` foreign key, `game`, `high_score`, `updated_at`, unique (`user_id`, `game`).

Treat the migration as documentation/reference. Compare it with the existing database before running any SQL against a live project.

## Known API contract

| Action | Purpose |
|---|---|
| `list_users` | Returns registered usernames for the console |
| `create_user` | Creates a player profile and directional password |
| `login` | Verifies a player and returns saved scores |
| `update_score` | Stores a player's best score for a game |
| `dashboard` | Returns usernames and scores for the dashboard |

Known game keys: `snake`, `pong`, `space_invaders`, `pacman`, `breakout`, `tetris`.

## Source fidelity and deployment status

The function source checked into this repository was reconstructed from the known API contract. It has **not been verified as an exact export of the currently deployed Edge Function**. The API, firmware, and dashboard have not all been validated together as a reproducible release. This folder is intended to document the project and its design, not provide a one-click redeployment recipe.

## Security limitations

The prototype stores directional passwords as plaintext and uses only four characters selected from `U`, `D`, `L`, and `R`. User-list and dashboard actions are exposed to public-key callers, and robust rate limiting and per-user authorization are not implemented. Do not use this approach for sensitive accounts or production authentication.

The service-role key bypasses normal row-level security protections. Keep it in server-side function secrets only; never place it in the firmware, browser dashboard, or Git history.
