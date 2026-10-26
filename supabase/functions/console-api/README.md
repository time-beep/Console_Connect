# console-api Edge Function

This function is the shared HTTP API used by the ESP32 console and web dashboard.

## Runtime secrets

The function reads these server-side environment variables:

- `SUPABASE_URL`
- `SUPABASE_SERVICE_ROLE_KEY`

Set them in Supabase Function Secrets. Never put the service-role key in the firmware, dashboard, or repository.

## Request format

Send a JSON POST to `/functions/v1/console-api` with an `action` field.

| Action | Extra fields | Purpose |
|---|---|---|
| `list_users` | none | List registered usernames |
| `create_user` | `user_id`, `password` | Create an allowed player |
| `login` | `user_id`, `password` | Return the player's saved scores |
| `update_score` | `user_id`, `password`, `game`, `score` | Save a new personal best |
| `dashboard` | none | Return all player names and high scores |

Password format in the prototype is four characters from `U`, `D`, `L`, `R`. The game keys are `snake`, `pong`, `space_invaders`, `pacman`, `breakout`, and `tetris`.

## Security warning

The current implementation is a prototype. Passwords are stored as plaintext, and dashboard/user-list actions are available to public-key callers. Do not use this as production authentication. Add proper password hashing/authentication, authorization, rate limiting, and restrict dashboard data before real-world use.
