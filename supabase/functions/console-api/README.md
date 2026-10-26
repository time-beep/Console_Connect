# `console-api` — Edge Function Notes

This folder documents the API layer intended to be shared by the ESP32 console and the web dashboard.

## Recorded API contract

The known actions are:

| Action | Purpose |
|---|---|
| `list_users` | List player usernames |
| `create_user` | Create a player profile |
| `login` | Verify a player and return saved scores |
| `update_score` | Save a player's best score for a game |
| `dashboard` | Return player names and scores for the dashboard |

The prototype uses four-character directional passwords (`U`, `D`, `L`, `R`) and game keys such as `snake`, `pong`, `space_invaders`, `pacman`, `breakout`, and `tetris`.

## Source fidelity

**Important:** `index.ts` was reconstructed for this repository from the known API contract. It has not been verified against an exact export of the currently deployed Supabase function. Treat it as a reference/documentation snapshot, not a guaranteed deployable copy of the live backend.

Before using this code to redeploy, compare it with the deployed implementation, verify request/response formats against the firmware and dashboard, and test the full flow.

## Security limitations

This is a learning prototype, not production authentication. The known implementation stores directional passwords as plaintext, has a very small password space, and exposes user-list/dashboard actions to public-key callers. It lacks robust authentication, per-user authorization, and rate limiting. Do not use it for sensitive accounts or data.

Any privileged Supabase service-role key must remain in server-side secrets and must never be committed to this repository, browser code, or firmware.
